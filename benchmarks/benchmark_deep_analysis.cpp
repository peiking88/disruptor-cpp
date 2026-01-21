/**
 * 深度分析：Raw 测试的各个环节性能
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

// 模拟 Java 的 VarHandle 方式：releaseFence + 普通写
class SequenceJavaStyle
{
public:
    static constexpr long INITIAL_VALUE = -1L;

    explicit SequenceJavaStyle(long initialValue = INITIAL_VALUE) noexcept
        : value(initialValue)
    {
    }

    long get() const noexcept
    {
        long v = value;
        std::atomic_thread_fence(std::memory_order_acquire);
        return v;
    }

    // Java style: releaseFence() + 普通写
    void set(long v) noexcept
    {
        std::atomic_thread_fence(std::memory_order_release);
        value = v;
    }

    // Java style volatile write
    void setVolatile(long v) noexcept
    {
        std::atomic_thread_fence(std::memory_order_release);
        value = v;
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

private:
    alignas(128) long value;
    char padding[120]{};
};

// 测试 1: Sequence set 性能对比
void compareSequenceSet()
{
    std::cout << "=== Sequence.set() Comparison ===\n";
    constexpr long iterations = 100'000'000L;

    // C++ atomic release store
    {
        disruptor::Sequence seq(0);
        auto start = Clock::now();
        for (long i = 0; i < iterations; ++i)
        {
            seq.set(i);
        }
        auto end = Clock::now();
        double elapsed = std::chrono::duration<double>(end - start).count();
        std::cout << "  C++ atomic release store: " << iterations / elapsed << " ops/s\n";
    }

    // Java-style fence + plain store
    {
        SequenceJavaStyle seq(0);
        auto start = Clock::now();
        for (long i = 0; i < iterations; ++i)
        {
            seq.set(i);
        }
        auto end = Clock::now();
        double elapsed = std::chrono::duration<double>(end - start).count();
        std::cout << "  Java-style fence+store:   " << iterations / elapsed << " ops/s\n";
    }

    std::cout << "\n";
}

// 测试 2: Producer-Consumer 同步开销
void compareProducerConsumerSync()
{
    std::cout << "=== Producer-Consumer Sync (200M events) ===\n";
    constexpr long iterations = 200'000'000L;
    constexpr int bufferSize = 1024 * 64;

    // 标准 Disruptor 方式
    {
        disruptor::YieldingWaitStrategy waitStrategy;
        disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);
        disruptor::Sequence consumerSequence(disruptor::Sequence::INITIAL_VALUE);
        sequencer.addGatingSequences({&consumerSequence});
        disruptor::SequenceBarrier barrier(waitStrategy, sequencer.getCursor(), {});

        std::atomic<bool> done{false};

        std::thread consumer([&] {
            long expected = iterations - 1;
            long processed = -1;
            try {
                do {
                    processed = barrier.waitFor(consumerSequence.get() + 1);
                    consumerSequence.set(processed);
                } while (processed < expected);
            } catch (const disruptor::AlertException&) {}
            done.store(true, std::memory_order_release);
        });

        auto start = Clock::now();
        for (long i = 0; i < iterations; ++i)
        {
            long next = sequencer.next();
            sequencer.publish(next);
        }
        while (!done.load(std::memory_order_acquire)) std::this_thread::yield();
        auto end = Clock::now();

        barrier.alert();
        consumer.join();

        double elapsed = std::chrono::duration<double>(end - start).count();
        std::cout << "  Standard Disruptor:       " << iterations / elapsed << " ops/s\n";
    }

    // 简化版：直接使用 Sequence，无 Sequencer 开销
    {
        disruptor::Sequence cursor(disruptor::Sequence::INITIAL_VALUE);
        disruptor::Sequence consumerSequence(disruptor::Sequence::INITIAL_VALUE);

        std::atomic<bool> done{false};

        std::thread consumer([&] {
            long expected = iterations - 1;
            long processed = -1;
            do {
                // Busy wait for new data
                long available;
                while ((available = cursor.get()) <= processed) {
                    #if defined(__x86_64__)
                        __builtin_ia32_pause();
                    #endif
                }
                processed = available;
                consumerSequence.set(processed);
            } while (processed < expected);
            done.store(true, std::memory_order_release);
        });

        auto start = Clock::now();
        for (long i = 0; i < iterations; ++i)
        {
            cursor.set(i);
        }
        while (!done.load(std::memory_order_acquire)) std::this_thread::yield();
        auto end = Clock::now();

        consumer.join();

        double elapsed = std::chrono::duration<double>(end - start).count();
        std::cout << "  Direct Sequence (no Seq): " << iterations / elapsed << " ops/s\n";
    }

    // Java-style Sequence
    {
        SequenceJavaStyle cursor(SequenceJavaStyle::INITIAL_VALUE);
        SequenceJavaStyle consumerSequence(SequenceJavaStyle::INITIAL_VALUE);

        std::atomic<bool> done{false};

        std::thread consumer([&] {
            long expected = iterations - 1;
            long processed = -1;
            do {
                long available;
                while ((available = cursor.get()) <= processed) {
                    #if defined(__x86_64__)
                        __builtin_ia32_pause();
                    #endif
                }
                processed = available;
                consumerSequence.set(processed);
            } while (processed < expected);
            done.store(true, std::memory_order_release);
        });

        auto start = Clock::now();
        for (long i = 0; i < iterations; ++i)
        {
            cursor.set(i);
        }
        while (!done.load(std::memory_order_acquire)) std::this_thread::yield();
        auto end = Clock::now();

        consumer.join();

        double elapsed = std::chrono::duration<double>(end - start).count();
        std::cout << "  Java-style Sequence:      " << iterations / elapsed << " ops/s\n";
    }

    std::cout << "\n";
}

// 测试 3: Sequencer 开销分解
void analyzeSequencerOverhead()
{
    std::cout << "=== Sequencer Overhead Analysis ===\n";
    constexpr long iterations = 100'000'000L;
    constexpr int bufferSize = 1024 * 64;

    disruptor::YieldingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);
    disruptor::Sequence gatingSeq(iterations + bufferSize);
    sequencer.addGatingSequences({&gatingSeq});

    // 单独测试 next()
    {
        auto start = Clock::now();
        for (long i = 0; i < iterations; ++i)
        {
            volatile long next = sequencer.next();
            (void)next;
        }
        auto end = Clock::now();
        double elapsed = std::chrono::duration<double>(end - start).count();
        std::cout << "  next() only:              " << iterations / elapsed << " ops/s\n";
    }

    // 单独测试 publish() - 需要先 reset
    disruptor::SingleProducerSequencer sequencer2(bufferSize, waitStrategy);
    sequencer2.addGatingSequences({&gatingSeq});
    
    // 预先获取所有序列号
    std::vector<long> sequences(iterations);
    for (long i = 0; i < iterations; ++i)
    {
        sequences[i] = sequencer2.next();
    }

    // 只测试 publish
    {
        disruptor::SingleProducerSequencer sequencer3(bufferSize, waitStrategy);
        sequencer3.addGatingSequences({&gatingSeq});
        
        auto start = Clock::now();
        for (long i = 0; i < iterations; ++i)
        {
            sequencer3.publish(i);
        }
        auto end = Clock::now();
        double elapsed = std::chrono::duration<double>(end - start).count();
        std::cout << "  publish() only:           " << iterations / elapsed << " ops/s\n";
    }

    // next + publish
    {
        disruptor::SingleProducerSequencer sequencer4(bufferSize, waitStrategy);
        sequencer4.addGatingSequences({&gatingSeq});
        
        auto start = Clock::now();
        for (long i = 0; i < iterations; ++i)
        {
            long next = sequencer4.next();
            sequencer4.publish(next);
        }
        auto end = Clock::now();
        double elapsed = std::chrono::duration<double>(end - start).count();
        std::cout << "  next() + publish():       " << iterations / elapsed << " ops/s\n";
    }

    std::cout << "\n";
}

int main()
{
    std::cout << "=== Deep Performance Analysis ===\n\n";

    // Warmup
    {
        disruptor::Sequence seq(0);
        for (int i = 0; i < 10'000'000; ++i) seq.set(i);
    }

    compareSequenceSet();
    analyzeSequencerOverhead();
    compareProducerConsumerSync();

    return 0;
}
