/*
 * C11 + POSIX Threads：手动实现静态分块与动态领任务（4 个工作线程）。
 * 每项任务计算 i*i，并用 nanosleep 模拟不同长度的工作。
 * sleep 用来观察重叠和负载分配，不是 CPU 密集计算的性能基准。
 * 仅供阅读和使用方法演示；本次交付未运行本程序。
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { WORKERS = 4, TASKS = 12 };
static const int delay_ms[TASKS] = {
    120, 110, 100, 20, 20, 20, 20, 20, 20, 20, 20, 20
};

typedef struct {
    bool dynamic;
    int next_task;              /* 动态分配时，尚未领取的第一个任务。 */
    int ready;
    bool start;
    pthread_mutex_t schedule_mutex;
    pthread_cond_t start_changed;
    pthread_mutex_t output_mutex;
    struct timespec started;
    int result[WORKERS];        /* 每个线程只写自己的槽，join 后主线程归约。 */
    int count[WORKERS];
} Team;

typedef struct {
    int id;
    Team *team;                /* 指向 main 中活到所有 join 完成的对象。 */
} Worker;

static void must(int error, const char *operation)
{
    if (error != 0) {
        fprintf(stderr, "%s: %s\n", operation, strerror(error));
        exit(EXIT_FAILURE);
    }
}

static struct timespec now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return t;
}

static double elapsed_ms(const Team *team)
{
    struct timespec t = now();
    return (double)(t.tv_sec - team->started.tv_sec) * 1000.0
         + (double)(t.tv_nsec - team->started.tv_nsec) / 1000000.0;
}

static void simulate_work(int milliseconds)
{
    struct timespec left = {
        milliseconds / 1000, (milliseconds % 1000) * 1000000L
    };
    /* 信号可中断 sleep；用返回的剩余时间继续等待。 */
    while (nanosleep(&left, &left) != 0) {
        if (errno != EINTR) {
            perror("nanosleep");
            exit(EXIT_FAILURE);
        }
    }
}

static void trace(Team *team, int worker, int task, const char *event)
{
    must(pthread_mutex_lock(&team->output_mutex), "lock output");
    printf("%7.2f ms  worker=%d task=%2d %s\n",
           elapsed_ms(team), worker, task + 1, event);
    must(pthread_mutex_unlock(&team->output_mutex), "unlock output");
}

static void *run_worker(void *argument)
{
    Worker *worker = argument;
    Team *team = worker->team;
    int id = worker->id;

    /* 手动实现一次性的出发闸门。条件变量等待会释放互斥量，
     * 醒来后重新加锁；使用 while 应对虚假唤醒。
     */
    must(pthread_mutex_lock(&team->schedule_mutex), "lock gate");
    ++team->ready;
    must(pthread_cond_broadcast(&team->start_changed), "announce ready");
    while (!team->start)
        must(pthread_cond_wait(&team->start_changed, &team->schedule_mutex),
             "wait start");
    must(pthread_mutex_unlock(&team->schedule_mutex), "unlock gate");

    int local_sum = 0;
    int completed = 0;
    /* 连续分块：前几个慢任务会集中在 worker 0，形成负载不均。 */
    int cursor = id * TASKS / WORKERS;
    int end = (id + 1) * TASKS / WORKERS;
    for (;;) {
        int task;
        if (team->dynamic) {
            must(pthread_mutex_lock(&team->schedule_mutex), "lock task index");
            if (team->next_task == TASKS) {
                must(pthread_mutex_unlock(&team->schedule_mutex), "unlock end");
                break;
            }
            task = team->next_task++;
            must(pthread_mutex_unlock(&team->schedule_mutex), "unlock task index");
        } else {
            if (cursor == end)
                break;
            task = cursor++;
        }

        trace(team, id, task, "begin");
        /* 领完任务立即释放锁。持锁 sleep 会使其他线程无法领任务。 */
        simulate_work(delay_ms[task]);
        int value = task + 1;
        local_sum += value * value;
        ++completed;
        trace(team, id, task, "end");
    }
    team->result[id] = local_sum;
    team->count[id] = completed;
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 2 || (strcmp(argv[1], "static") != 0 &&
                      strcmp(argv[1], "dynamic") != 0)) {
        fprintf(stderr, "usage: %s static|dynamic\n", argv[0]);
        return EXIT_FAILURE;
    }
    Team team = {0};
    team.dynamic = strcmp(argv[1], "dynamic") == 0;
    must(pthread_mutex_init(&team.schedule_mutex, NULL), "init schedule mutex");
    must(pthread_mutex_init(&team.output_mutex, NULL), "init output mutex");
    must(pthread_cond_init(&team.start_changed, NULL), "init start condition");
    pthread_t threads[WORKERS];
    Worker workers[WORKERS];
    for (int id = 0; id < WORKERS; ++id) {
        workers[id].id = id;
        workers[id].team = &team;
        must(pthread_create(&threads[id], NULL, run_worker, &workers[id]),
             "create worker");
    }

    must(pthread_mutex_lock(&team.schedule_mutex), "lock start");
    while (team.ready != WORKERS)
        must(pthread_cond_wait(&team.start_changed, &team.schedule_mutex),
             "wait all ready");
    team.started = now();
    team.start = true;
    must(pthread_cond_broadcast(&team.start_changed), "start workers");
    must(pthread_mutex_unlock(&team.schedule_mutex), "unlock start");

    int sum = 0;
    int completed = 0;
    for (int id = 0; id < WORKERS; ++id) {
        must(pthread_join(threads[id], NULL), "join worker");
        sum += team.result[id];
        completed += team.count[id];
    }
    double elapsed = elapsed_ms(&team);
    for (int id = 0; id < WORKERS; ++id)
        printf("worker=%d tasks=%d partial_sum=%d\n",
               id, team.count[id], team.result[id]);
    printf("mode=%s tasks=%d sum=%d expected=650 elapsed=%.2f ms\n",
           argv[1], completed, sum, elapsed);
    must(pthread_cond_destroy(&team.start_changed), "destroy condition");
    must(pthread_mutex_destroy(&team.schedule_mutex), "destroy schedule mutex");
    must(pthread_mutex_destroy(&team.output_mutex), "destroy output mutex");
    return sum == 650 && completed == TASKS ? EXIT_SUCCESS : EXIT_FAILURE;
}
