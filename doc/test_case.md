# 测试用例清单

## 单元/功能测试（JUnit，`src/test/java`）
| 测试用例 | 说明 |
| --- | --- |
| `AggregateEventHandlerTest` | 测试聚合事件处理器分发事件给所有子处理器的功能。 |
| `BatchEventProcessorTest` | 测试批处理事件处理器的循环、生命周期与异常处理功能。 |
| `BatchingTest` | 测试事件批量发布与消费路径的功能。 |
| `BusySpinWaitStrategyTest` | 测试 BusySpin 等待策略的等待/唤醒语义。 |
| `DisruptorStressTest` | 测试高并发/高吞吐场景下 Disruptor 的正确性。 |
| `EventPollerTest` | 测试事件轮询器取数、状态与序列推进的功能。 |
| `EventPublisherTest` | 测试事件发布器对 RingBuffer 的发布流程与字段填充功能。 |
| `EventTranslatorTest` | 测试事件转换器的变换与链式翻译功能。 |
| `FatalExceptionHandlerTest` | 测试致命异常处理器的传播与关闭行为。 |
| `FixedSequenceGroupTest` | 测试固定序列组的最小序列计算与更新功能。 |
| `IgnoreExceptionHandlerTest` | 测试忽略异常处理器对异常的吞掉语义。 |
| `LifecycleAwareTest` | 测试生命周期回调（onStart/onShutdown）的触发顺序。 |
| `LiteTimeoutBlockingWaitStrategyTest` | 测试轻量超时阻塞等待的超时与唤醒功能。 |
| `MaxBatchSizeEventProcessorTest` | 测试事件处理器遵循最大批量大小限制的功能。 |
| `MultiProducerSequencerTest` | 测试多生产者序列器的并发发布、有序性与容量校验功能。 |
| `PhasedBackoffWaitStrategyTest` | 测试分阶段退避等待策略的相位切换与超时功能。 |
| `RewindBatchEventProcessorTest` | 测试批处理器在重绕/重试场景下的序列处理功能。 |
| `RingBufferTest` | 测试 RingBuffer 的容量、发布、覆盖与读写边界功能。 |
| `RingBufferWithAssertingStubTest` | 测试 RingBuffer 配合断言桩的发布/消费验证功能。 |
| `SequenceBarrierTest` | 测试序列屏障的等待、警报与中断语义。 |
| `SequenceGroupTest` | 测试序列组的添加/移除/最小序列计算功能。 |
| `SequenceReportingCallbackTest` | 测试序列上报回调的调用与数值正确性功能。 |
| `SequenceTest` | 测试 Sequence 自增、CAS 与可见性行为。 |
| `SequencerTest` | 测试 Sequencer 申请序号、发布与 gating 序列规则功能。 |
| `ShutdownOnFatalExceptionTest` | 测试出现致命异常时的自动关停流程。 |
| `SingleProducerSequencerTest` | 测试单生产者序列器的发布、有序性与 wrap 处理功能。 |
| `SleepingWaitStrategyTest` | 测试 Sleeping 等待策略的等待/退避语义。 |
| `TimeoutBlockingWaitStrategyTest` | 测试阻塞等待策略的超时与异常处理功能。 |
| `YieldingWaitStrategyTest` | 测试 Yielding 等待策略的让出/响应行为。 |
| `dsl/ConsumerRepositoryTest` | 测试 DSL 消费者仓库的注册、查询与生命周期管理功能。 |
| `dsl/DisruptorTest` | 测试 DSL 层 Disruptor 构建、事件处理拓扑与发布流程。 |
| `util/UtilTest` | 测试工具方法（如取最近 2 次幂、取整数对齐等）的正确性。 |

## 性能 / 吞吐 / 延迟测试（`src/perftest/java`）
| 测试用例 | 说明 |
| --- | --- |
| `AbstractPerfTestDisruptor` | 测试 Disruptor 性能基线（公共驱动）。 |
| `AbstractPerfTestQueue` | 测试队列基准性能基线。 |
| `PerfTestContext` | 测试性能场景的上下文与共享配置支撑。 |
| `immutable/CustomPerformanceTest` | 测试不可变事件的自定义性能场景。 |
| `immutable/SimplePerformanceTest` | 测试不可变事件的基础性能场景。 |
| `offheap/OneToOneOffHeapThroughputTest` | 测试 1:1 非堆通路吞吐性能。 |
| `offheap/OneToOneOnHeapThroughputTest` | 测试 1:1 堆内通路吞吐性能。 |
| `queue/OneToOneQueueThroughputTest` | 测试队列 1:1 吞吐性能。 |
| `queue/OneToOneQueueBatchedThroughputTest` | 测试队列 1:1 批量吞吐性能。 |
| `queue/OneToThreeQueueThroughputTest` | 测试队列 1:3 吞吐性能。 |
| `queue/OneToThreeDiamondQueueThroughputTest` | 测试队列 1:3 菱形拓扑吞吐性能。 |
| `queue/OneToThreePipelineQueueThroughputTest` | 测试队列 1:3 管线拓扑吞吐性能。 |
| `queue/PingPongQueueLatencyTest` | 测试队列乒乓往返延迟性能。 |
| `queue/ThreeToOneQueueThroughputTest` | 测试队列 3:1 吞吐性能。 |
| `queue/ThreeToOneQueueBatchThroughputTest` | 测试队列 3:1 批量吞吐性能。 |
| `raw/OneToOneRawThroughputTest` | 测试原始数组/缓冲 1:1 吞吐性能。 |
| `raw/OneToOneRawBatchThroughputTest` | 测试原始 1:1 批量吞吐性能。 |
| `translator/OneToOneTranslatorThroughputTest` | 测试使用 Translator 的 1:1 吞吐性能。 |
| `sequenced/OneToOneSequencedThroughputTest` | 测试 Sequenced 1:1 吞吐性能。 |
| `sequenced/OneToOneSequencedBatchThroughputTest` | 测试 Sequenced 1:1 批量吞吐性能。 |
| `sequenced/OneToOneSequencedLongArrayThroughputTest` | 测试 Sequenced 1:1 长整型数组吞吐性能。 |
| `sequenced/OneToOneSequencedPollerThroughputTest` | 测试 Sequenced 1:1 Poller 吞吐性能。 |
| `sequenced/OneToThreeSequencedThroughputTest` | 测试 Sequenced 1:3 吞吐性能。 |
| `sequenced/OneToThreeDiamondSequencedThroughputTest` | 测试 Sequenced 1:3 菱形拓扑吞吐性能。 |
| `sequenced/OneToThreePipelineSequencedThroughputTest` | 测试 Sequenced 1:3 管线拓扑吞吐性能。 |
| `sequenced/PingPongSequencedLatencyTest` | 测试 Sequenced 乒乓往返延迟性能。 |
| `sequenced/ThreeToOneSequencedThroughputTest` | 测试 Sequenced 3:1 吞吐性能。 |
| `sequenced/ThreeToOneSequencedBatchThroughputTest` | 测试 Sequenced 3:1 批量吞吐性能。 |
| `sequenced/ThreeToThreeSequencedThroughputTest` | 测试 Sequenced 3:3 吞吐性能。 |
| 支撑：`support/PerfTestUtil` | 测试基准工具与通用支撑逻辑。 |
| 支撑：`support/ValueAddition*` | 测试基准中的值累加处理/处理器支撑。 |

## 并发压力 / 一致性测试（JCStress，`src/jcstress/java`）
| 测试用例 | 说明 |
| --- | --- |
| `LoggerInitializationStress` | 测试 Logger 初始化在并发下的一致性与可见性。 |
| `MultiProducerSequencerUnsafeStress` | 测试多生产者（Unsafe 路径）发布序列的一致性。 |
| `MultiProducerSequencerVarHandleStress` | 测试多生产者（VarHandle 路径）发布序列的一致性。 |
| `SequenceStressUnsafe` | 测试 Sequence（Unsafe 路径）的并发增量可见性。 |
| `SequenceStressVarHandle` | 测试 Sequence（VarHandle 路径）的并发增量可见性。 |
| `SequenceStressVarHandleBarrier` | 测试 Sequence VarHandle + barrier 的并发可见性与有序性。 |

## JMH 基准（生成代码，`build/jmh-generated-sources` & `build/generated/sources/annotationProcessor/java/jmh`）
| 测试用例 | 说明 |
| --- | --- |
| `*_jmhTest.java` / `*_jmhType*.java` | 测试吞吐/延迟等微基准的 JMH 自动生成包装类（如 `RingBufferBenchmark_*`, `SequenceBenchmark_*`, `BlockingQueueBenchmark_*`, `ArrayAccessBenchmark_*` 等）。 |
