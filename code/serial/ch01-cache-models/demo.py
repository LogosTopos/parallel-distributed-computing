"""第一章 1.6—1.10：计算与单缓存行模拟，仅使用 Python 标准库。"""


class NoCoherence:
    """每个处理器缓存一个值；故意不通知其他处理器和 DMA 设备。"""

    def __init__(self, policy):
        self.policy = policy
        self.memory = 0
        self.cache = [None, None]
        self.dirty = [False, False]

    def read(self, cpu):
        if self.cache[cpu] is None:
            self.cache[cpu] = self.memory
        return self.cache[cpu]

    def write(self, cpu, value):
        self.cache[cpu] = value
        if self.policy == "WT":
            self.memory = value
        else:
            self.dirty[cpu] = True

    def clean(self, cpu):
        if self.dirty[cpu]:
            self.memory = self.cache[cpu]
            self.dirty[cpu] = False

    def invalidate(self, cpu):
        # 本实验只丢弃干净副本，不能用它直接丢掉尚未写回的新值。
        if self.dirty[cpu]:
            raise ValueError("clean a dirty line before invalidating it")
        self.cache[cpu] = None

    def dma_write(self, value):
        self.memory = value

    def dma_read(self):
        return self.memory

    def state(self):
        copies = ["I" if v is None else str(v) for v in self.cache]
        copies = [v + ("*" if d else "") for v, d in zip(copies, self.dirty)]
        return f"P1={copies[0]}, P2={copies[1]}, Mem={self.memory}"


class CoherentWB:
    """单地址、操作串行完成的 WB 模型；owner 保管尚未写回的最新值。"""

    def __init__(self, policy, ncpu=3):
        if policy not in ("update", "invalidate"):
            raise ValueError("choose update or invalidate")
        self.policy = policy
        self.memory = 0
        self.cache = [None] * ncpu
        self.owner = None
        self.reset_stats()

    def reset_stats(self):
        self.stats = dict(updates=0, invalidations=0, misses=0,
                          cache_transfers=0, writebacks=0)

    def read(self, cpu):
        if self.cache[cpu] is None:
            self.stats["misses"] += 1
            if self.owner is None:
                self.cache[cpu] = self.memory
            else:
                self.cache[cpu] = self.cache[self.owner]
                self.stats["cache_transfers"] += 1
        return self.cache[cpu]

    def write(self, cpu, value):
        self.read(cpu)
        peers = [i for i, v in enumerate(self.cache)
                 if i != cpu and v is not None]
        if peers:
            if self.policy == "update":
                self.stats["updates"] += 1
                for i in peers:
                    self.cache[i] = value
            else:
                self.stats["invalidations"] += 1
                for i in peers:
                    self.cache[i] = None
        self.cache[cpu] = value
        self.owner = cpu

    def writeback(self):
        if self.owner is not None:
            self.memory = self.cache[self.owner]
            self.owner = None
            self.stats["writebacks"] += 1

    def state(self):
        copies = [f"P{i + 1}={'I' if v is None else v}"
                  for i, v in enumerate(self.cache)]
        owner = "none" if self.owner is None else f"P{self.owner + 1}"
        memory = str(self.memory) + ("(stale)" if self.owner is not None else "")
        return ", ".join(copies) + f", Mem={memory}, owner={owner}"


def calculations():
    print("[1.6] dot product, one word/element, 2 FLOP/pair")
    frequency = 1e9
    words_per_access = 4
    for name, cycles in (("L1", 1), ("DRAM", 100)):
        seconds = cycles / frequency
        bandwidth = words_per_access / seconds
        print(f"{name}: {seconds * 1e9:g} ns/access, {bandwidth:.0f} word/s")
        flops_per_word = 2 / 2
        print(f"dot-product memory ceiling: {bandwidth * flops_per_word / 1e6:g} MFLOPS")
    print("[1.7] effective access time")
    average_ns = sum(p * t for p, t in ((0.8, 10), (0.1, 100), (0.1, 400)))
    print(f"average = {average_ns:g} ns, rate = {1e3 / average_ns:.6f} Maccess/s")
    print(f"one access/operation: {1e3 / average_ns:.6f} Mop/s")


def migration():
    print("\n[1.8] WT, migrate P2 -> P1")
    m = NoCoherence("WT")
    m.read(0)
    m.write(1, 1)
    print("after P2 writes 1: " + m.state())
    print(f"migrated process reads on P1: {m.read(0)} (expected 1)")
    m.invalidate(0)
    print(f"after invalidating P1: {m.read(0)}")

    print("[1.8] WB, migrate P1 -> P2")
    m = NoCoherence("WB")
    m.read(0)
    m.write(0, 1)
    print("after P1 writes 1: " + m.state())
    print(f"migrated process reads on P2: {m.read(1)} (expected 1)")
    m.clean(0)
    m.invalidate(1)
    print(f"after clean P1 + invalidate P2: {m.read(1)}")


def dma():
    print("\n[1.9] DMA input with WT")
    m = NoCoherence("WT")
    m.read(0)
    m.read(1)
    m.dma_write(1)
    print("after device writes 1: " + m.state())
    print(f"CPU reads: P1={m.read(0)}, P2={m.read(1)} (expected 1, 1)")
    m.invalidate(0)
    m.invalidate(1)
    print(f"after invalidation: P1={m.read(0)}, P2={m.read(1)}")

    print("[1.9] DMA output with WB")
    m = NoCoherence("WB")
    m.read(0)
    m.read(1)
    m.write(0, 1)
    print("after P1 writes 1: " + m.state())
    print(f"device reads: {m.dma_read()} (expected 1)")
    m.clean(0)
    print(f"after cleaning P1, device reads: {m.dma_read()}")
    print(f"P2 still reads: {m.read(1)} (CPU coherence is a separate issue)")


def protocols():
    for policy in ("invalidate", "update"):
        print(f"\n[1.10] WB + {policy}")
        m = CoherentWB(policy)
        for cpu in range(3):
            m.read(cpu)
        m.reset_stats()
        print("before write: " + m.state())
        m.write(0, 1)
        print("after P1 writes 1: " + m.state())
        print(f"P2 reads: {m.read(1)}")
        print("after P2 reads: " + m.state())
        m.writeback()
        print("after writeback: " + m.state())
        print("events: " + str(m.stats))


def workload(policy, read_each_write):
    m = CoherentWB(policy)
    for cpu in range(3):
        m.read(cpu)
    m.reset_stats()
    for value in (1, 2, 3):
        m.write(0, value)
        if read_each_write:
            m.read(1)
            m.read(2)
    if not read_each_write:
        m.read(1)
        m.read(2)
    return m


def traffic():
    print("\n[comparison] counters after warmup, no final writeback")
    print("pattern policy updates invalidations misses cache_transfers memory latest")
    for name, read_each in (("burst", False), ("interleaved", True)):
        for policy in ("update", "invalidate"):
            m = workload(policy, read_each)
            s = m.stats
            print(f"{name} {policy} {s['updates']} {s['invalidations']} "
                  f"{s['misses']} {s['cache_transfers']} {m.memory} {m.read(1)}")


if __name__ == "__main__":
    calculations()
    migration()
    dma()
    protocols()
    traffic()
