/**
 * 详细性能分析基准测试
 * 
 * 用于分析 C++ vs Java 性能差异的根本原因
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "disruptor/sequence.h"
#include "disruptor/consumer_barrier.h"
#include "disruptor/producer_sequencer.h"
#include "disruptor/wait_strategy.h"

using Clock = std::chrono::steady_clock;

// 测试 1: 纯 Sequence 操作性能
void benchSequenceOps()
{
    std::cout << "=== 1. Sequence Operations ===\n";
    constexpr long iterations = 100'000'000L;

    disruptor::Sequence seq(0);

    // set + get 组合
    auto start = Clock::now();
    for (long i = 0; i < iterations; ++i)
    {
        seq.set(i);
        volatile long v = seq.get();
        (void)v;
    }
    auto end = Clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  set+get: " << iterations / elapsed << " ops/s\n";
    std::cout << "           " << elapsed * 1e9 / iterations << " ns/op\n";

    // 纯 set
    start = Clock::now();
    for (long i = 0; i < iterations; ++i)
    {
        seq.set(i);
    }
    end = Clock::now();
    elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  set only: " << iterations / elapsed << " ops/s\n";
    std::cout << "            " << elapsed * 1e9 / iterations << " ns/op\n";

    std::cout << "\n";
}

// 测试 2: Sequencer next+publish (无消费者等待)
void benchSequencerProducerOnly()
{
    std::cout << "=== 2. Sequencer Producer-Only (no wait) ===\n";
    constexpr long iterations = 100'000'000L;
    constexpr int bufferSize = 1024 * 64;

    disruptor::YieldingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    // 添加一个永远不会阻塞的 gating sequence
    disruptor::Sequence gatingSeq(iterations + bufferSize);
    sequencer.addGatingSequences({&gatingSeq});

    auto start = Clock::now();
    for (long i = 0; i < iterations; ++i)
    {
        long next = sequencer.next();
        sequencer.publish(next);
    }
    auto end = Clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  next+publish: " << iterations / elapsed << " ops/s\n";
    std::cout << "                " << elapsed * 1e9 / iterations << " ns/op\n";
    std::cout << "\n";
}

// 测试 3: SequenceBarrier waitFor 性能 (数据已就绪)
void benchBarrierWaitReady()
{
    std::cout << "=== 3. SequenceBarrier waitFor (data ready) ===\n";
    constexpr long iterations = 100'000'000L;

    disruptor::YieldingWaitStrategy waitStrategy;
    disruptor::Sequence cursor(iterations);  // 数据全部就绪

    disruptor::SequenceBarrier barrier(waitStrategy, cursor, {});

    auto start = Clock::now();
    for (long i = 0; i < iterations; ++i)
    {
        volatile long available = barrier.waitFor(i);
        (void)available;
    }
    auto end = Clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  waitFor (ready): " << iterations / elapsed << " ops/s\n";
    std::cout << "                   " << elapsed * 1e9 / iterations << " ns/op\n";
    std::cout << "\n";
}

// 测试 4: 完整流水线 (Producer + Consumer)
void benchFullPipeline()
{
    std::cout << "=== 4. Full Pipeline (Producer + Consumer) ===\n";
    constexpr long iterations = 100'000'000L;
    constexpr int bufferSize = 1024 * 64;

    disruptor::YieldingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    disruptor::Sequence consumerSequence(disruptor::Sequence::INITIAL_VALUE);
    sequencer.addGatingSequences({&consumerSequence});

    disruptor::SequenceBarrier barrier(waitStrategy, sequencer.getCursor(), {});

    std::atomic<bool> done{false};

    std::thread consumer([&] {
        long expected = iterations - 1;
        long processed = -1;

        try
        {
            do
            {
                processed = barrier.waitFor(consumerSequence.get() + 1);
                consumerSequence.set(processed);
            } while (processed < expected);
        }
        catch (const disruptor::AlertException&) {}

        done.store(true, std::memory_order_release);
    });

    auto start = Clock::now();

    for (long i = 0; i < iterations; ++i)
    {
        long next = sequencer.next();
        sequencer.publish(next);
    }

    while (!done.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }

    auto end = Clock::now();

    barrier.alert();
    consumer.join();

    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  Full pipeline: " << iterations / elapsed << " ops/s\n";
    std::cout << "                 " << elapsed * 1e9 / iterations << " ns/op\n";
    std::cout << "\n";
}

// 测试 5: 内联优化测试 - 避免虚函数调用
void benchInlinedPath()
{
    std::cout << "=== 5. Inlined Path (no virtual calls) ===\n";
    constexpr long iterations = 100'000'000L;
    constexpr int bufferSize = 1024 * 64;

    // 直接使用具体类型，避免虚函数调用
    disruptor::YieldingWaitStrategy waitStrategy;
    
    // 简化的 inline sequencer - 模拟 Java 的 JIT 优化
    disruptor::Sequence cursor(disruptor::Sequence::INITIAL_VALUE);
    disruptor::Sequence gatingSeq(iterations + bufferSize);
    long nextValue = disruptor::Sequence::INITIAL_VALUE;

    auto start = Clock::now();
    for (long i = 0; i < iterations; ++i)
    {
        // Inlined next()
        long nextSeq = ++nextValue;
        
        // Inlined publish()
        cursor.set(nextSeq);
    }
    auto end = Clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  Inlined next+publish: " << iterations / elapsed << " ops/s\n";
    std::cout << "                        " << elapsed * 1e9 / iterations << " ns/op\n";
    std::cout << "\n";
}

// 测试 6: 批量发布
void benchBatchPublish()
{
    std::cout << "=== 6. Batch Publish ===\n";
    constexpr long totalEvents = 100'000'000L;
    constexpr int batchSize = 100;
    constexpr long batches = totalEvents / batchSize;
    constexpr int bufferSize = 1024 * 64;

    disruptor::YieldingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    disruptor::Sequence gatingSeq(totalEvents + bufferSize);
    sequencer.addGatingSequences({&gatingSeq});

    auto start = Clock::now();
    for (long i = 0; i < batches; ++i)
    {
        long hi = sequencer.next(batchSize);
        sequencer.publish(hi - batchSize + 1, hi);
    }
    auto end = Clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  Batch publish (size=" << batchSize << "): " << totalEvents / elapsed << " events/s\n";
    std::cout << "                                  " << elapsed * 1e9 / totalEvents << " ns/event\n";
    std::cout << "\n";
}

// 测试 7: 原子操作基础开销
void benchAtomicBaseline()
{
    std::cout << "=== 7. Atomic Operations Baseline ===\n";
    constexpr long iterations = 100'000'000L;

    std::atomic<long> value{0};

    // relaxed store
    auto start = Clock::now();
    for (long i = 0; i < iterations; ++i)
    {
        value.store(i, std::memory_order_relaxed);
    }
    auto end = Clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  relaxed store: " << iterations / elapsed << " ops/s\n";

    // release store
    start = Clock::now();
    for (long i = 0; i < iterations; ++i)
    {
        value.store(i, std::memory_order_release);
    }
    end = Clock::now();
    elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  release store: " << iterations / elapsed << " ops/s\n";

    // seq_cst store
    start = Clock::now();
    for (long i = 0; i < iterations; ++i)
    {
        value.store(i, std::memory_order_seq_cst);
    }
    end = Clock::now();
    elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  seq_cst store: " << iterations / elapsed << " ops/s\n";

    // acquire load
    start = Clock::now();
    for (long i = 0; i < iterations; ++i)
    {
        volatile long v = value.load(std::memory_order_acquire);
        (void)v;
    }
    end = Clock::now();
    elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  acquire load: " << iterations / elapsed << " ops/s\n";

    std::cout << "\n";
}

int main()
{
    std::cout << "=== C++ Disruptor Performance Analysis ===\n\n";

    // Warmup
    std::cout << "Warmup...\n";
    {
        disruptor::Sequence seq(0);
        for (int i = 0; i < 10'000'000; ++i)
        {
            seq.set(i);
            volatile long v = seq.get();
            (void)v;
        }
    }
    std::cout << "\n";

    benchAtomicBaseline();
    benchSequenceOps();
    benchSequencerProducerOnly();
    benchBarrierWaitReady();
    benchInlinedPath();
    benchBatchPublish();
    benchFullPipeline();

    return 0;
}
