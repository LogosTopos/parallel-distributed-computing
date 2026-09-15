# 目录约定

| 路径 | 用途 |
| --- | --- |
| `notes/architecture/` | 多核处理器、存储组织、互连网络与并行体系结构 |
| `notes/algorithms/` | 计算模型、性能分析、PCAM 与并行算法 |
| `notes/programming/` | 并行编程、同步、通信与运行环境 |
| `notes/distributed-systems/` | 逻辑时钟、互斥、共识、一致性与容错 |
| `discussions/` | 按日期保存讨论；已整理的结论链接到专题笔记 |
| `code/serial/` | 串行实现，供正确性和性能对照 |
| `code/openmp/` | OpenMP 共享存储实验 |
| `code/mpi/` | MPI 消息传递实验 |
| `code/cuda/` | GPU / CUDA 实验 |
| `code/distributed/` | 分布式算法与系统实验 |
| `exercises/chen-guoliang-3e/chNN/` | 原书第 NN 章习题解答 |
| `references/` | 参考书目信息 |

笔记采用 Markdown，文件名用简短英文主题，例如 `memory-organization.md`。讨论记录用 `YYYY-MM-DD-topic.md`，相关图片放在同目录的 `assets/` 下，按需建立。

习题文件采用 `exNN.md`，对应原书该章第 NN 题；保留原题编号，记录题意、分析和解答。涉及实现时链接 `code/` 中的对应实验，避免重复存放代码。各章目录已预留，尚未填写解答。

每个代码实验单独建目录，放源代码和简短运行说明。涉及性能比较时，记录硬件、编译器、编译参数、进程或线程数、输入规模和计时方法。实际需要时再添加构建配置与依赖文件。
