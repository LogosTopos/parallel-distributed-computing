# OpenMP 用法：从手动线程实现映射到 API

这里只介绍使用方法，代码片段未编译、未运行，也不包含在自动构建或测试中。语义参照 OpenMP 5.2；具体工具链需要支持相应功能。

## 从任务分配到 parallel for

下面是可保存为单个 `.c` 文件的完整用法示例，计算 1² + … + 12² = 650。与 [C 手动线程示例](../../code/pthreads/ch01-task-scheduling/demo.c) 的数学任务一致，这里省略用于放大时间差的 sleep。

```c
#include <stdio.h>

int main(void)
{
    int sum = 0;
    /* parallel 建立线程团队；for 分配迭代；每次领取一个迭代。
     * reduction 为参与计算的线程维护局部和，结束时合并到 sum。
     */
    #pragma omp parallel for num_threads(4) schedule(dynamic, 1) reduction(+:sum)
    for (int i = 1; i <= 12; ++i) {
        sum += i * i;
    }
    /* parallel 区域结束后，汇总已经完成。 */
    printf("sum=%d\n", sum);
    return sum == 650 ? 0 : 1;
}
```

在已经安装 OpenMP 支持的 GCC 工具链中，通常通过 `-fopenmp` 启用；Clang 的运行库配置依平台而异。本次不安装运行库或执行该示例。

| 手动实现 | OpenMP 表达 | 注意点 |
| --- | --- | --- |
| 创建和等待工作线程 | `parallel` 区域 | 请求线程数不一定等于实际团队大小 |
| 预先分块 | `schedule(static)` | 默认分块的具体边界不保证与本项目手写公式完全一致 |
| 空闲后继续领一项 | `schedule(dynamic, 1)` | 每次领取有调度开销，未必比 static 更快 |
| 每线程局部和再合并 | `reduction(+:sum)` | 浮点归约可能因结合顺序变化产生舍入差异 |

上述工作分配与归约语义见 [parallel](https://www.openmp.org/spec-html/5.2/openmpse57.html) 和 [reduction](https://www.openmp.org/spec-html/5.2/openmpsu52.html)。

## atomic、critical 与 barrier 分别解决什么

- `#pragma omp atomic update` 可以保护支持的单个更新表达式，例如 `completed += 1;`。同一并发访问协议内，其他对该变量的访问也必须正确同步。
- `#pragma omp critical` 保护一段临界区，适合多条操作共同维护一个约束；需要协调的线程须使用相同的命名临界区（或都使用未命名临界区）。
- `#pragma omp barrier` 让团队线程在阶段边界等待；不能只让某一个线程进入这个 barrier。

这些是三种不同的用途。把每次求和都放进 critical 虽然可保护更新，却会让线程竞争同一个串行区；适合归约的问题通常更直接地用 reduction 表达。规范入口见 [OpenMP 5.2 同步构造](https://www.openmp.org/spec-html/5.2/openmp.html)。

## flush 不等于教材中的写回主存

OpenMP 的 `flush` 是内存模型中的同步操作。它不保证把所有物理缓存中的脏数据立即写入 DRAM，也不能单独消除数据竞争。不能用它代替互斥、原子操作或正确的数据交接；`volatile` 同样不能替代线程同步。参见 [OpenMP 内存一致性规则](https://www.openmp.org/spec-html/5.2/openmpsu14.html)。

因此，C 模型中的 `nc_clean()` 和 C++ 模型中的 `CLEAN/DATA` 是我们显式定义的状态／消息操作，不能简单翻译成 `#pragma omp flush`。

## DMA 是另一种缓冲区交接

Linux 驱动中的典型流程是：为设备建立 DMA 映射、检查映射错误、让设备使用缓冲区、确认设备完成，再按映射类型交还 CPU。复用流式映射时，适用场景下用 `dma_sync_single_for_cpu()` 和 `dma_sync_single_for_device()` 表达双方交接。

这些是内核设备 API，方向、生命周期与平台都会影响行为；不能把普通用户态指针直接当作设备地址，或在设备仍使用缓冲区时随意并发修改。这里仅介绍用途，不提供或执行驱动代码。详见 [Linux DMA 官方指南](https://www.kernel.org/doc/html/latest/core-api/dma-api-howto.html)。
