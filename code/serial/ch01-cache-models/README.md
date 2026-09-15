# 第一章缓存模型实验

对应[《第一章习题选解》](../../../exercises/chen-guoliang-3e/ch01/README.md)中的 1.6—1.10 题。

Python 3.10 及以上，无第三方依赖。在仓库根目录运行：

```bash
python3 code/serial/ch01-cache-models/demo.py
python3 -m unittest discover -s code/serial/ch01-cache-models -p 'test_*.py' -v
```

| 文件 | 内容 |
| --- | --- |
| [demo.py](demo.py) | 计算公式、无一致性缓存、WB 写更新/写无效化模型与演示 |
| [results.txt](results.txt) | Python 3.12.14 的实际运行输出 |
| [test_demo.py](test_demo.py) | 旧值复现、DMA 维护与协议读写行为测试 |

`0` 表示旧值，`1` 表示新值，`I` 表示没有有效缓存副本，`*` 表示脏副本。`owner` 是尚未写回的数据所有者；`Mem=0(stale)` 表示此时不能直接使用主存的旧值响应读取。

实验是单地址、串行事件的逻辑模拟，不使用真实线程，也不对本机 CPU 缓存测速。对比表统计预热后的模型事件数，不代表真实硬件耗时。
