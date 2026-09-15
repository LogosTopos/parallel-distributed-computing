/*
 * 第一章 1.8—1.10：单地址、串行事件的缓存教学模型（C11）。
 * 阅读顺序：数据结构 -> 无一致性读写 -> WB 协议 -> 题目演示 -> 自检。
 * 所有“CPU”和“设备”都是普通变量；这里没有线程、OpenMP 或真实 DMA。
 * 0 是旧值 X，1 是新值 X'；数组下标 0、1、2 对应 P1、P2、P3。
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CPU_COUNT = 3, NO_OWNER = -1 };
typedef enum { WT, WB } WritePolicy;
typedef enum { INVALIDATE, UPDATE } CoherencePolicy;

typedef struct {
    int value;                 /* valid=false 时，value 的残留内容不可用于读取。 */
    bool valid;
    bool dirty;                /* 仅用于无一致性模型；协议模型统一使用 owner。 */
} CacheLine;

typedef struct {
    int memory;
    CacheLine cache[CPU_COUNT];
    WritePolicy policy;
} NoCoherence;

typedef struct {
    unsigned updates, invalidations;  /* 一次写影响多个接收者也只算一次广播。 */
    unsigned misses, transfers, writebacks;
} Counters;

typedef struct {
    int memory;
    CacheLine cache[CPU_COUNT];
    int owner;                 /* 谁负责尚未写回的数据；-1 表示主存可供数。 */
    CoherencePolicy policy;
    Counters events;
} CoherentWB;

/* 不用 assert 执行有副作用的操作，避免 -DNDEBUG 使自检悄悄消失。 */
static void check(bool ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void check_cpu(int cpu)
{
    check(cpu >= 0 && cpu < CPU_COUNT, "CPU index out of range");
}

static NoCoherence nc_init(WritePolicy policy)
{
    NoCoherence m = {0};        /* 初始主存为 0，所有缓存无效、无脏数据。 */
    m.policy = policy;
    return m;
}

static int nc_read(NoCoherence *m, int cpu)
{
    check_cpu(cpu);
    if (!m->cache[cpu].valid) {
        m->cache[cpu].value = m->memory;
        m->cache[cpu].valid = true;
    }
    return m->cache[cpu].value;
}

static void nc_write(NoCoherence *m, int cpu, int value)
{
    check_cpu(cpu);
    /* 单地址的整值写入；直接建立有效副本，不模拟写分配的取整行流量。 */
    m->cache[cpu].value = value;
    m->cache[cpu].valid = true;
    m->cache[cpu].dirty = m->policy == WB;
    if (m->policy == WT)
        m->memory = value;
    /* 故意不检查其他 CPU：WT 更新主存，并不自动更新它们的旧副本。 */
}

static void nc_clean(NoCoherence *m, int cpu)
{
    check_cpu(cpu);
    if (m->cache[cpu].dirty) {
        m->memory = m->cache[cpu].value;
        m->cache[cpu].dirty = false;
    }
}

static bool nc_invalidate(NoCoherence *m, int cpu)
{
    check_cpu(cpu);
    /* 返回失败并保留原状；直接丢弃脏副本会丢失未写回的修改。 */
    if (m->cache[cpu].dirty)
        return false;
    m->cache[cpu].valid = false;
    return true;
}

static CoherentWB wb_init(CoherencePolicy policy)
{
    CoherentWB m = {0};
    m.owner = NO_OWNER;
    m.policy = policy;
    return m;
}

static int wb_read(CoherentWB *m, int cpu)
{
    check_cpu(cpu);
    if (!m->cache[cpu].valid) {
        ++m->events.misses;
        if (m->owner == NO_OWNER) {
            m->cache[cpu].value = m->memory;
        } else {
            /* 主存可能落后：从承担写回责任的缓存取得最新值。 */
            m->cache[cpu].value = m->cache[m->owner].value;
            ++m->events.transfers;
        }
        m->cache[cpu].valid = true;
    }
    return m->cache[cpu].value;
}

static void wb_write(CoherentWB *m, int cpu, int value)
{
    /* 先取得有效副本，再改变其他缓存，最后转交写回责任。
     * 模拟写分配；这一步也使不同 CPU 交替写入时的供数路径可见。
     */
    (void)wb_read(m, cpu);
    bool has_peer = false;
    for (int peer = 0; peer < CPU_COUNT; ++peer) {
        if (peer == cpu || !m->cache[peer].valid)
            continue;
        has_peer = true;
        if (m->policy == UPDATE)
            m->cache[peer].value = value;
        else
            m->cache[peer].valid = false;
    }
    if (has_peer) {
        if (m->policy == UPDATE)
            ++m->events.updates;
        else
            ++m->events.invalidations;
    }
    m->cache[cpu].value = value;
    m->owner = cpu;
    /* WB：本次写入不会修改 memory。广播在这个函数返回前完成。 */
}

static void wb_writeback(CoherentWB *m)
{
    if (m->owner != NO_OWNER) {
        m->memory = m->cache[m->owner].value;
        m->owner = NO_OWNER;
        ++m->events.writebacks;
    }
}

static void wb_evict(CoherentWB *m, int cpu)
{
    check_cpu(cpu);
    /* 教学用的显式逐出，不实现替换算法。所有者必须先完成写回。 */
    if (m->owner == cpu)
        wb_writeback(m);
    m->cache[cpu].valid = false;
}

static void print_copies(const CacheLine cache[], int count)
{
    for (int cpu = 0; cpu < count; ++cpu) {
        printf("P%d=", cpu + 1);
        if (cache[cpu].valid)
            printf("%d%s", cache[cpu].value, cache[cpu].dirty ? "*" : "");
        else
            printf("I");
        printf(", ");
    }
}

static void nc_state(const NoCoherence *m)
{
    print_copies(m->cache, 2);   /* 1.8、1.9 只涉及两个处理器。 */
    printf("Mem=%d\n", m->memory);
}

static void wb_state(const CoherentWB *m)
{
    print_copies(m->cache, CPU_COUNT);
    printf("Mem=%d%s, owner=", m->memory,
           m->owner == NO_OWNER ? "" : "(stale)");
    if (m->owner == NO_OWNER)
        puts("none");
    else
        printf("P%d\n", m->owner + 1);
}

/* 每一步先完成读写，再打印；不依赖函数实参之间的求值顺序。 */
static void migration_and_io(void)
{
    puts("[1.8] WT: migrate P2 -> P1");
    NoCoherence m = nc_init(WT);
    (void)nc_read(&m, 0);
    nc_write(&m, 1, 1);
    nc_state(&m);
    int value = nc_read(&m, 0);
    check(value == 0, "WT migration must expose stale cache");
    printf("P1 reads %d (latest=1)\n", value);
    check(nc_invalidate(&m, 0), "invalidate clean P1");
    value = nc_read(&m, 0);
    check(value == 1, "WT migration repair");
    printf("after invalidate P1: %d\n", value);

    puts("\n[1.8] WB: migrate P1 -> P2");
    m = nc_init(WB);
    nc_write(&m, 0, 1);
    nc_state(&m);
    value = nc_read(&m, 1);
    check(value == 0, "WB migration must expose stale memory");
    printf("P2 reads %d (latest=1)\n", value);
    check(!nc_invalidate(&m, 0), "refuse to discard dirty P1");
    nc_clean(&m, 0);
    value = nc_read(&m, 1);
    check(value == 0, "clean alone does not repair P2 cache");
    printf("after clean P1 only, P2 still reads %d\n", value);
    check(nc_invalidate(&m, 1), "invalidate stale P2");
    value = nc_read(&m, 1);
    check(value == 1, "WB migration repair");
    printf("after invalidate P2 too: %d\n", value);

    puts("\n[1.9] DMA input with WT");
    m = nc_init(WT);
    (void)nc_read(&m, 0);
    (void)nc_read(&m, 1);
    m.memory = 1;              /* 手动模拟设备输入：只写主存，绕过全部缓存。 */
    nc_state(&m);
    for (int cpu = 0; cpu < 2; ++cpu) {
        value = nc_read(&m, cpu);
        check(value == 0, "DMA input must expose stale CPU copy");
        printf("P%d reads %d (latest=1)\n", cpu + 1, value);
        check(nc_invalidate(&m, cpu), "invalidate after device completion");
        check(nc_read(&m, cpu) == 1, "DMA input repair");
    }
    nc_state(&m);

    puts("\n[1.9] DMA output with WB");
    m = nc_init(WB);
    (void)nc_read(&m, 1);
    nc_write(&m, 0, 1);
    nc_state(&m);
    value = m.memory;          /* 手动模拟设备输出：直接读取主存。 */
    check(value == 0, "DMA output must expose stale memory");
    printf("device reads %d (latest=1)\n", value);
    nc_clean(&m, 0);           /* 写回完成之后，才把缓冲区交给设备。 */
    check(m.memory == 1, "DMA output repair");
    printf("after clean P1, device reads %d\n", m.memory);
    value = nc_read(&m, 1);
    check(value == 0, "device repair does not repair P2");
    printf("P2 still reads %d\n", value);
}

static const char *policy_name(CoherencePolicy policy)
{
    return policy == UPDATE ? "update" : "invalidate";
}

static void warmup(CoherentWB *m)
{
    for (int cpu = 0; cpu < CPU_COUNT; ++cpu)
        (void)wb_read(m, cpu);
    Counters zero = {0};
    m->events = zero;          /* 访问模式比较不统计初次装入缓存的成本。 */
}

static void protocols(void)
{
    const CoherencePolicy policies[] = {INVALIDATE, UPDATE};
    for (int p = 0; p < 2; ++p) {
        CoherentWB m = wb_init(policies[p]);
        printf("\n[1.10] WB + %s\n", policy_name(m.policy));
        warmup(&m);
        wb_state(&m);
        wb_write(&m, 0, 1);
        wb_state(&m);
        int value = wb_read(&m, 1);
        check(value == 1 && m.memory == 0, "read latest while memory is stale");
        printf("P2 reads %d\n", value);
        wb_state(&m);
        wb_write(&m, 1, 2);     /* 换一个写者，观察 owner 从 P1 交给 P2。 */
        puts("P2 writes 2 (ownership handoff):");
        wb_state(&m);
        wb_evict(&m, 1);
        puts("evict P2 (owner writes back before leaving):");
        wb_state(&m);
        check(wb_read(&m, 2) == 2, "read after owner eviction");
    }
}

static CoherentWB workload(CoherencePolicy policy, bool read_each_write)
{
    CoherentWB m = wb_init(policy);
    warmup(&m);
    for (int value = 1; value <= 3; ++value) {
        wb_write(&m, 0, value);
        if (read_each_write) {
            (void)wb_read(&m, 1);
            (void)wb_read(&m, 2);
        }
    }
    if (!read_each_write) {
        (void)wb_read(&m, 1);
        (void)wb_read(&m, 2);
    }
    return m;
}

static void traffic(void)
{
    puts("\n[comparison] after warmup, no final writeback");
    puts("pattern policy updates invalidations misses transfers memory latest");
    const CoherencePolicy policies[] = {UPDATE, INVALIDATE};
    for (int interleaved = 0; interleaved <= 1; ++interleaved) {
        for (int p = 0; p < 2; ++p) {
            CoherentWB m = workload(policies[p], interleaved != 0);
            Counters s = m.events;
            printf("%s %s %u %u %u %u %d %d\n",
                   interleaved ? "interleaved" : "burst", policy_name(m.policy),
                   s.updates, s.invalidations, s.misses, s.transfers,
                   m.memory, m.cache[1].value);
            unsigned broadcasts = interleaved ? 3u : 1u;
            check(s.updates == (m.policy == UPDATE ? 3u : 0u), "update count");
            check(s.invalidations == (m.policy == INVALIDATE ? broadcasts : 0u),
                  "invalidation count");
            check(s.misses == (m.policy == INVALIDATE ? 2u * broadcasts : 0u),
                  "read miss count");
            check(s.transfers == s.misses && s.writebacks == 0, "traffic window");
            check(m.memory == 0 && m.cache[1].value == 3, "workload final value");
        }
    }
}

/* 独立参照是最近一次写入值，不从模拟器的 owner 或主存推导答案。 */
static void check_invariants(const CoherentWB *m, int latest)
{
    check(m->owner >= NO_OWNER && m->owner < CPU_COUNT, "owner range");
    for (int cpu = 0; cpu < CPU_COUNT; ++cpu) {
        if (m->cache[cpu].valid)
            check(m->cache[cpu].value == latest, "valid copy must be latest");
    }
    if (m->owner == NO_OWNER)
        check(m->memory == latest, "memory must be current without owner");
    else
        check(m->cache[m->owner].valid && m->cache[m->owner].value == latest,
              "owner must retain latest data");
}

/* 有界枚举所有长度不超过 5 的操作序列：3 读、3 写、1 写回、3 逐出。
 * 结构体按值复制即可回溯，没有随机数、测试框架或动态内存。
 * 写入值在不同层取 0/1，覆盖“重新写回原值”的情况。
 */
static unsigned explore(CoherentWB m, int latest, int remaining)
{
    check_invariants(&m, latest);
    if (remaining == 0)
        return 0;
    unsigned visited = 0;
    for (int op = 0; op < 10; ++op) {
        CoherentWB next = m;
        int expected = latest;
        if (op < 3) {
            check(wb_read(&next, op) == expected, "enumerated read");
        } else if (op < 6) {
            expected = remaining % 2;
            wb_write(&next, op - 3, expected);
        } else if (op == 6) {
            wb_writeback(&next);
        } else {
            wb_evict(&next, op - 7);
        }
        visited += 1 + explore(next, expected, remaining - 1);
    }
    return visited;
}

int main(int argc, char *argv[])
{
    bool test = argc == 2 && strcmp(argv[1], "--test") == 0;
    if (argc != 1 && !test) {
        fprintf(stderr, "usage: %s [--test]\n", argv[0]);
        return EXIT_FAILURE;
    }
    migration_and_io();
    protocols();
    traffic();
    if (test) {
        unsigned visited = explore(wb_init(INVALIDATE), 0, 5);
        visited += explore(wb_init(UPDATE), 0, 5);
        check(visited == 222220u, "complete bounded enumeration");
        printf("\nPASS: scenarios, counters, %u protocol transitions\n", visited);
    }
    return EXIT_SUCCESS;
}
