# disruptor-cpp 需求文档

## 1. 项目概述
- 项目名称：`disruptor-cpp`
- 项目目标：参考 Java 版 Disruptor 的核心思想与接口语义，用 C++23 实现一个高性能的事件交换框架。
- 参考依据：`PROJECT_DOC.md` 中的技术亮点、类结构与 API 描述。

## 2. 设计目标
1. 提供面向低延迟、高吞吐的环形缓冲区事件发布/消费机制。
2. 支持单生产者/多生产者模式，支持单消费者/多消费者组合（广播、流水线、依赖图）。
3. 保持内存预分配、尽量减少分配与 GC/堆压力。
4. 可选的等待策略，实现阻塞/自旋/让出/短暂休眠等等待模式。
5. 接口简洁、可组合，易于构建处理链与依赖关系。

## 3. 功能性需求
### 3.1 核心数据结构
- `RingBuffer<T>`：固定大小、2 的幂容量的环形缓冲区，支持预分配事件对象。
- `Sequence`：并发序列计数器，提供原子读写与 CAS/自增等操作。
- `Sequencer`：序列分配与发布协调，提供单生产者/多生产者实现。
- `SequenceBarrier`：消费侧屏障，负责等待可用序列与依赖序列推进。

### 3.2 生产者/消费者模式
- 生产者：支持 `SINGLE` 与 `MULTI` 两种模式。
- 消费者：
  - 单消费者（1P/MP -> 1C）
  - 广播多消费者（multicast）
  - 流水线串行（pipeline）
  - 依赖图/扇出-汇合（fan-out/fan-in）

### 3.3 事件处理
- `EventHandler<T>`：用户处理回调接口（`onEvent(T&, sequence, endOfBatch)`）。
- `EventProcessor` 与 `BatchEventProcessor`：提供标准消费线程模型。

### 3.4 等待策略
- `WaitStrategy` 抽象接口：
  - `BlockingWaitStrategy`
  - `BusySpinWaitStrategy`
  - `YieldingWaitStrategy`
  - `SleepingWaitStrategy`

### 3.5 其他约束
- `RingBuffer` 容量必须为 2 的幂。
- `Sequence` 等关键结构尽量避免 false sharing。

## 4. 非功能性需求
- 编译标准：C++23。
- 性能：支持高吞吐下的低延迟发布/消费。
- 线程安全：多生产者场景要求线程安全；单生产者在单线程中提供更低同步开销。

## 5. 测试与验收
### 5.1 单元测试
- `Sequence` 原子操作语义。
- 单生产者/单消费者正常发布与消费。
- 多生产者发布与单消费者消费的正确性。

### 5.2 性能测试
- 基准测试：单生产者/单消费者在指定事件数下的吞吐量（events/sec）。

### 5.3 验收标准
- 所有单元测试通过。
- 基准测试可运行并输出吞吐统计。

## 6. 构建要求
- 使用 CMake 构建。
- 支持 `-j$(nproc)` 并行构建。
- 提供 `ctest` 或独立可执行测试程序。
