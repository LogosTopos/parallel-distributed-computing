"""用最近一次写入的值作参照，检查缓存副本和所有者交接。"""

import random
import unittest

from demo import CoherentWB, NoCoherence, workload


class CacheModelTests(unittest.TestCase):
    def test_migration_exposes_old_values(self):
        for policy, writer, reader in (("WT", 1, 0), ("WB", 0, 1)):
            with self.subTest(policy=policy):
                m = NoCoherence(policy)
                m.read(0)
                m.write(writer, 1)
                self.assertEqual(m.read(reader), 0)
                m.clean(writer)
                m.invalidate(reader)
                self.assertEqual(m.read(reader), 1)

    def test_dma_maintenance(self):
        m = NoCoherence("WT")
        m.read(0)
        m.dma_write(7)
        self.assertEqual(m.read(0), 0)
        m.invalidate(0)
        self.assertEqual(m.read(0), 7)
        m = NoCoherence("WB")
        m.write(0, 9)
        self.assertEqual(m.dma_read(), 0)
        with self.assertRaises(ValueError):
            m.invalidate(0)
        m.clean(0)
        self.assertEqual(m.dma_read(), 9)

    def test_coherent_traces_against_single_value(self):
        for policy in ("update", "invalidate"):
            with self.subTest(policy=policy):
                rng = random.Random(20260915)
                m = CoherentWB(policy)
                latest = 0
                for _ in range(1000):
                    cpu = rng.randrange(3)
                    op = rng.choice(("read", "write", "writeback"))
                    if op == "write":
                        latest = rng.randrange(100)
                        m.write(cpu, latest)
                    elif op == "read":
                        self.assertEqual(m.read(cpu), latest)
                    else:
                        m.writeback()
                    for value in m.cache:
                        if value is not None:
                            self.assertEqual(value, latest)
                    if m.owner is None:
                        self.assertEqual(m.memory, latest)
                    else:
                        self.assertEqual(m.cache[m.owner], latest)

    def test_workloads_end_with_latest_value(self):
        for policy in ("update", "invalidate"):
            for read_each in (False, True):
                m = workload(policy, read_each)
                self.assertEqual(m.memory, 0)
                self.assertEqual([m.read(i) for i in range(3)], [3, 3, 3])
                m.writeback()
                self.assertEqual(m.memory, 3)


if __name__ == "__main__":
    unittest.main()
