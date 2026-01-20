# moodycamel Lock-free Queues (Deep Dive)

本文基于 `external/concurrentqueue` 与 `external/readerwriterqueue` 的源码实现与注释，整理其无锁队列的技术细节。

## 1) `moodycamel::ConcurrentQueue`（MPMC，非阻塞）

### 1.1 生产者/消费者令牌与调度策略
- 提供 `ProducerToken`/`ConsumerToken`，建议每线程一个 token，用于减少开销并定位到具体 producer 子队列。`ProducerToken` 在析构时会标记 producer 为 inactive，便于回收复用。`ConsumerToken` 维护消费轮换状态（全局 offset、当前/目标 producer 等）。
```414:758:/home/li/disruptor-cpp/external/concurrentqueue/concurrentqueue.h
```
- `try_dequeue` 会扫描生产者链表，根据 `size_approx` 选择更可能有数据的 producer；带 `ConsumerToken` 的 dequeue 有显式“轮转”机制（全局 offset + 每个 producer 固定配额后触发轮转），用于降低热点并均衡消费。 
```1120:1267:/home/li/disruptor-cpp/external/concurrentqueue/concurrentqueue.h
```

### 1.2 显式/隐式生产者模型
- 队列区分 **ExplicitProducer**（通过 token 创建）和 **ImplicitProducer**（基于线程 ID 的隐式哈希），内部都继承 `ProducerBase`，维护各自的 `headIndex/tailIndex` 与 dequeue 相关计数。 
```1694:1757:/home/li/disruptor-cpp/external/concurrentqueue/concurrentqueue.h
```
- 隐式生产者通过线程 ID 的 hash 表进行映射，hash 表支持扩容，并使用 CAS 插入；当线程退出时标记 producer 为可回收。 
```3290:3571:/home/li/disruptor-cpp/external/concurrentqueue/concurrentqueue.h
```

### 1.3 Block 结构与“空”判定
- 数据以 **Block** 为基本单元（固定 `BLOCK_SIZE`），`Block` 内部维护 `elementsCompletelyDequeued` 计数或 `emptyFlags`，用以判断是否完全清空；显式/隐式上下文在“空判定”上有不同实现路径。 
```1553:1684:/home/li/disruptor-cpp/external/concurrentqueue/concurrentqueue.h
```

### 1.4 显式生产者：Block 索引 + 预分配
- 显式 producer 使用 **BlockIndex** 结构来追踪区间与 block 映射；当 block 使用满，会尝试复用后继空 block，否则从全局池/空闲链表中获取新 block。 
```1764:2393:/home/li/disruptor-cpp/external/concurrentqueue/concurrentqueue.h
```

### 1.5 隐式生产者：BlockIndex（hash + ring）
- 隐式 producer 维护自己的 block index（散列表 + index 数组），入队时可能插入新的 block index entry；出队时通过 index 查找 block。 
```2881:3013:/home/li/disruptor-cpp/external/concurrentqueue/concurrentqueue.h
```

### 1.6 内存池与回收策略
- 通过 **初始 block pool**、**全局 free list** 和 **动态分配** 结合管理 block；`RECYCLE_ALLOCATED_BLOCKS` 控制是否回收动态 block，否则直接释放。 
```3039:3113:/home/li/disruptor-cpp/external/concurrentqueue/concurrentqueue.h
```

### 1.7 FreeList（CAS + 引用计数）
- 全局 free list 采用 CAS + 引用计数位（`REFS_MASK`/`SHOULD_BE_ON_FREELIST`）管理节点生命周期，保障在并发下的正确性。 
```1433:1546:/home/li/disruptor-cpp/external/concurrentqueue/concurrentqueue.h
```

### 1.8 批量入队/出队
- 提供 bulk 入队/出队接口；bulk 版本会预先分配足够 block，批量构造/移动元素，并在异常时回滚已构建元素。 
```2054:2245:/home/li/disruptor-cpp/external/concurrentqueue/concurrentqueue.h
```

---

## 2) `moodycamel::BlockingConcurrentQueue`
- `BlockingConcurrentQueue` 包装 `ConcurrentQueue`，对每次 enqueue 进行 `sema->signal()`，并在 `wait_dequeue` 等 API 上使用轻量信号量阻塞等待。 
```21:454:/home/li/disruptor-cpp/external/concurrentqueue/blockingconcurrentqueue.h
```
- `size_approx()` 直接基于信号量计数估算队列大小。 
```532:550:/home/li/disruptor-cpp/external/concurrentqueue/blockingconcurrentqueue.h
```

---

## 3) `LightweightSemaphore`（用于阻塞队列）
- 以 **原子计数 + 自旋 + 系统信号量** 组合实现：先自旋 CAS 减计数，失败再进入系统 semaphore 等待，兼顾低延迟与低开销。 
```268:418:/home/li/disruptor-cpp/external/concurrentqueue/lightweightsemaphore.h
```

---

## 4) `moodycamel::ReaderWriterQueue`（SPSC）

### 4.1 结构与并发模型
- **SPSC**（单生产者/单消费者）队列，采用“队列中的队列”结构：底层 block 为环形缓冲区，block 之间通过环形链表串联；producer 只更新 tail，consumer 只更新 front。 
```22:96:/home/li/disruptor-cpp/external/readerwriterqueue/readerwriterqueue.h
```

### 4.2 Block 对齐与伪共享规避
- Block 内对 front/tail 等高争用变量进行 cache line 分离填充，减少 false sharing。 
```695:706:/home/li/disruptor-cpp/external/readerwriterqueue/readerwriterqueue.h
```

### 4.3 内存屏障与轻量原子
- `atomicops.h` 提供跨平台内存屏障（`fence`/`compiler_fence`）与 `weak_atomic`（非完整 `std::atomic`，仅保证硬件层原子性与低序语义）。
```102:241:/home/li/disruptor-cpp/external/readerwriterqueue/atomicops.h
```
```255:352:/home/li/disruptor-cpp/external/readerwriterqueue/atomicops.h
```

### 4.4 可阻塞版本
- `BlockingReaderWriterQueue` 组合 `ReaderWriterQueue` 与 SPSC 轻量信号量，提供 `wait_dequeue`/`wait_dequeue_timed`。 
```757:952:/home/li/disruptor-cpp/external/readerwriterqueue/readerwriterqueue.h
```

---

## 5) `ReaderWriterCircularBuffer`（固定容量 SPSC）
- 固定容量环形缓冲区，容量向上取 2 的幂并使用 mask 做快速取模；内部通过 `slots_`/`items` 两个信号量管理空位和元素数量。 
```34:51:/home/li/disruptor-cpp/external/readerwriterqueue/readerwritercircularbuffer.h
```
```95:221:/home/li/disruptor-cpp/external/readerwriterqueue/readerwritercircularbuffer.h
```

---

## 6) 关键技术特点小结（对比视角）
- **ConcurrentQueue（MPMC）**：显式/隐式 producer 双路径；每 producer 子队列 + 全局消费者轮转；Block + BlockIndex + FreeList 组合管理；支持 bulk 以减少开销。
- **ReaderWriterQueue（SPSC）**：结构更轻量，以 block 环 + cache line padding + 弱原子/显式 fence 保证低成本并发。
- **Blocking 版本**：均采用轻量信号量（自旋 + 系统 semaphore）在无元素时阻塞等待。 
```268:418:/home/li/disruptor-cpp/external/concurrentqueue/lightweightsemaphore.h
```
```757:952:/home/li/disruptor-cpp/external/readerwriterqueue/readerwriterqueue.h
```
