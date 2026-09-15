/*
 * C++17：3 个缓存节点 + 1 个 home 节点的 WB 写无效化教学模型。
 * 各缓存节点运行在独立线程；home 在主线程处理请求。
 * 节点只能通过消息交换缓存数据，不直接访问其他节点的缓存变量。
 * 手动实现有界环形信箱、待处理请求队列、目录和失效确认。
 * sleep_for 模拟消息传输和节点工作延迟；本次交付未运行本程序。
 */
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

constexpr int nodes = 3;
constexpr int inbox_capacity = 16;
enum class Kind { Read, Write, ReadDone, WriteDone, Invalidate, Ack,
                  Fetch, Data, Clean, Done, Stop };

struct Message {
    Kind kind;
    int from;
    int value = 0;             // Read/Invalidate/Ack 等控制消息不使用 value。
};

const char *name(Kind kind)
{
    switch (kind) {
    case Kind::Read: return "READ";
    case Kind::Write: return "WRITE";
    case Kind::ReadDone: return "READ_DONE";
    case Kind::WriteDone: return "WRITE_DONE";
    case Kind::Invalidate: return "INVALIDATE";
    case Kind::Ack: return "ACK";
    case Kind::Fetch: return "FETCH";
    case Kind::Data: return "DATA";
    case Kind::Clean: return "CLEAN";
    case Kind::Done: return "DONE";
    case Kind::Stop: return "STOP";
    }
    return "unknown";
}

void require(bool ok, const char *message)
{
    if (!ok) {
        std::cerr << "protocol error: " << message << '\n';
        std::abort();          // 模型不继续执行已经破坏协议的状态。
    }
}

struct Inbox {
    std::array<Message, inbox_capacity> slots{};
    int head = 0, tail = 0, count = 0;
    std::mutex mutex;
    std::condition_variable readable, writable;
};

struct Network {
    std::array<Inbox, nodes + 1> inboxes; // 0 是 home，1—3 是缓存节点。
    std::mutex output;
    const std::chrono::steady_clock::time_point started =
        std::chrono::steady_clock::now();
};

void pause_ms(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void log(Network &network, int node, const char *event, int value, int from = -1)
{
    std::lock_guard<std::mutex> lock(network.output);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - network.started).count();
    std::cout << elapsed << " ms node=" << node << ' ' << event;
    if (from >= 0)
        std::cout << " from=" << from;
    std::cout << " value=" << value << '\n';
}

void send(Network &network, int to, Message message)
{
    // 每次发送阻塞发送者 5 ms，其他线程仍可推进。不持锁睡眠。
    // 这是可靠、同一发送者到同一接收者有序的传输模型，不是实测网络。
    pause_ms(5);
    Inbox &box = network.inboxes[to];
    std::unique_lock<std::mutex> lock(box.mutex);
    box.writable.wait(lock, [&box] { return box.count < inbox_capacity; });
    box.slots[box.tail] = message;
    box.tail = (box.tail + 1) % inbox_capacity;
    ++box.count;
    lock.unlock();
    box.readable.notify_one();
}

Message receive(Network &network, int node)
{
    Inbox &box = network.inboxes[node];
    std::unique_lock<std::mutex> lock(box.mutex);
    box.readable.wait(lock, [&box] { return box.count > 0; });
    Message message = box.slots[box.head];
    box.head = (box.head + 1) % inbox_capacity;
    --box.count;
    lock.unlock();
    box.writable.notify_one();
    log(network, node, name(message.kind), message.value, message.from);
    return message;
}

struct Cache {
    bool valid = false;
    bool dirty = false;
    int value = 0;
};

// 节点等待自己的读写回复时，也必须响应别人的失效和取数请求。
// 若只等 READ_DONE/WRITE_DONE 而不处理这些消息，会形成互相等待。
bool service(Network &network, int id, Cache &cache, Message message)
{
    require(message.from == 0, "cache accepts control messages only from home");
    if (message.kind == Kind::Invalidate) {
        // 本模型写请求覆盖整个单值。home 已保存待提交的新值，
        // 因而可以授权丢弃被覆盖的旧脏值；这不等于逐出时可以丢脏数据。
        cache.valid = false;
        cache.dirty = false;
        send(network, 0, {Kind::Ack, id}); // 先无效，再确认。
        return true;
    }
    if (message.kind == Kind::Fetch || message.kind == Kind::Clean) {
        require(cache.valid && cache.dirty, "owner must have dirty latest value");
        int value = cache.value;
        if (message.kind == Kind::Clean)
            cache.dirty = false;
        send(network, 0, {Kind::Data, id, value});
        return true;
    }
    return false;
}

void request(Network &network, int id, Cache &cache, Kind operation, int value = 0)
{
    // 读命中是真正的节点本地操作；不会去偷看 home 的值。
    if (operation == Kind::Read && cache.valid) {
        log(network, id, "LOCAL_HIT", cache.value);
        return;
    }
    send(network, 0, {operation, id, value});
    Kind expected = operation == Kind::Read ? Kind::ReadDone : Kind::WriteDone;
    for (;;) {
        Message message = receive(network, id);
        if (service(network, id, cache, message))
            continue;
        require(message.kind == expected, "unexpected client reply");
        cache.value = message.value;
        cache.valid = true;
        cache.dirty = operation == Kind::Write;
        log(network, id, "APPLICATION_COMPLETED", cache.value);
        return;
    }
}

void run_node(Network &network, int id)
{
    Cache cache;               // 只有本线程能访问该节点的缓存对象。
    request(network, id, cache, Kind::Read);
    pause_ms(id * 15);         // 模拟各节点独立工作；不保证确定的跨节点顺序。
    if (id != 3)
        request(network, id, cache, Kind::Write, id * 10);
    pause_ms(30);
    request(network, id, cache, Kind::Read);
    send(network, 0, {Kind::Done, id});

    // 应用结束不代表缓存控制器离线：它可能仍是 owner，必须继续供数。
    for (;;) {
        Message message = receive(network, id);
        if (message.kind == Kind::Stop) {
            require(message.from == 0, "stop must come from home");
            require(!cache.valid || cache.value == message.value, "final valid copy");
            require(!cache.dirty, "final writeback must be complete");
            return;
        }
        require(service(network, id, cache, message), "unexpected idle message");
    }
}

struct Home {
    int memory = 0;
    int owner = 0;             // 0 表示无脏所有者，主存可直接供数。
    std::array<bool, nodes + 1> sharers{};
    // home 等 ACK/DATA 时，新来的应用请求不能丢弃，也不能抢先执行。
    std::array<Message, nodes> pending{};
    int head = 0, tail = 0, count = 0;
};

bool is_request(Kind kind)
{
    return kind == Kind::Read || kind == Kind::Write || kind == Kind::Done;
}

Message wait_reply(Network &network, Home &home)
{
    for (;;) {
        Message message = receive(network, 0);
        require(message.from >= 1 && message.from <= nodes, "sender range");
        if (!is_request(message.kind))
            return message;
        // 每节点至多一个未完成应用请求；因此 nodes 个槽足够。
        require(home.count < nodes, "pending queue full");
        home.pending[home.tail] = message;
        home.tail = (home.tail + 1) % nodes;
        ++home.count;
    }
}

int fetch_owner(Network &network, Home &home, Kind kind)
{
    if (home.owner == 0)
        return home.memory;
    send(network, home.owner, {kind, 0});
    Message reply = wait_reply(network, home);
    require(reply.kind == Kind::Data && reply.from == home.owner, "owner DATA");
    return reply.value;
}

void run_home(Network &network)
{
    Home home;                // 目录和主存只由主线程访问；不与缓存线程共享。
    int finished = 0;
    int latest_committed = 0;  // 仅用于结尾核对，不参与 READ 的供数路径。
    std::array<bool, nodes + 1> done{};
    while (finished < nodes) {
        Message message;
        if (home.count > 0) {
            message = home.pending[home.head];
            home.head = (home.head + 1) % nodes;
            --home.count;
        } else {
            message = receive(network, 0);
        }
        int requester = message.from;
        require(requester >= 1 && requester <= nodes, "requester range");
        require(is_request(message.kind), "home expects application request");
        if (message.kind == Kind::Done) {
            require(!done[requester], "duplicate DONE");
            done[requester] = true;
            ++finished;
        } else if (message.kind == Kind::Read) {
            int value = fetch_owner(network, home, Kind::Fetch);
            home.sharers[requester] = true;
            send(network, requester, {Kind::ReadDone, 0, value});
            // FETCH 只供数，保留脏所有者；这里没有顺便更新主存。
        } else {
            std::array<bool, nodes + 1> waiting{};
            int acknowledgements = 0;
            for (int peer = 1; peer <= nodes; ++peer) {
                if (peer != requester && home.sharers[peer]) {
                    waiting[peer] = true;
                    ++acknowledgements;
                    send(network, peer, {Kind::Invalidate, 0});
                }
            }
            while (acknowledgements > 0) {
                Message reply = wait_reply(network, home);
                require(reply.kind == Kind::Ack && waiting[reply.from],
                        "expected unique invalidation ACK");
                waiting[reply.from] = false;
                --acknowledgements;
            }
            // 全部旧读者确认失效后，才授予写权限并允许应用完成写入。
            for (int peer = 1; peer <= nodes; ++peer)
                home.sharers[peer] = peer == requester;
            home.owner = requester;
            latest_committed = message.value;
            send(network, requester, {Kind::WriteDone, 0, message.value});
            log(network, 0, "WRITE_COMMITTED", message.value);
            log(network, 0, "MEMORY_STILL", home.memory);
        }
    }

    // 先取回最新脏数据并写回，再关闭所有节点；不能先 join 后向 owner 取数。
    home.memory = fetch_owner(network, home, Kind::Clean);
    home.owner = 0;
    require(home.memory == latest_committed, "final memory equals last committed write");
    log(network, 0, "FINAL_WRITEBACK", home.memory);
    for (int id = 1; id <= nodes; ++id)
        send(network, id, {Kind::Stop, 0, home.memory});
}

int main()
{
    Network network;
    std::array<std::thread, nodes> workers;
    for (int id = 1; id <= nodes; ++id)
        workers[id - 1] = std::thread(run_node, std::ref(network), id);
    run_home(network);
    for (std::thread &worker : workers)
        worker.join();
    std::cout << "All nodes stopped after final writeback.\n";
}
