#include <atomic>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "disruptor/exceptions.h"
#include "disruptor/producer_sequencer.h"
#include "disruptor/wait_strategy.h"

// SequencerTest - 测试 Sequencer 申请序号、发布与 gating 序列规则功能

// ========== 工具函数测试 ==========
TEST_CASE("isPowerOfTwo should return true for powers of two", "[sequencer][util]")
{
    REQUIRE(disruptor::isPowerOfTwo(1));
    REQUIRE(disruptor::isPowerOfTwo(2));
    REQUIRE(disruptor::isPowerOfTwo(4));
    REQUIRE(disruptor::isPowerOfTwo(8));
    REQUIRE(disruptor::isPowerOfTwo(16));
    REQUIRE(disruptor::isPowerOfTwo(1024));
    REQUIRE(disruptor::isPowerOfTwo(4096));
}

TEST_CASE("isPowerOfTwo should return false for non-powers of two", "[sequencer][util]")
{
    REQUIRE_FALSE(disruptor::isPowerOfTwo(0));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(-1));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(3));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(5));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(6));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(7));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(100));
}

TEST_CASE("log2i should return correct logarithm", "[sequencer][util]")
{
    REQUIRE(disruptor::log2i(1) == 0);
    REQUIRE(disruptor::log2i(2) == 1);
    REQUIRE(disruptor::log2i(4) == 2);
    REQUIRE(disruptor::log2i(8) == 3);
    REQUIRE(disruptor::log2i(16) == 4);
    REQUIRE(disruptor::log2i(1024) == 10);
}

// ========== SingleProducerSequencerTest ==========
TEST_CASE("SingleProducerSequencer should have correct buffer size", "[sequencer][single]")
{
    constexpr int bufferSize = 1024;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    REQUIRE(sequencer.getBufferSize() == bufferSize);
}

TEST_CASE("SingleProducerSequencer should start with initial cursor", "[sequencer][single]")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    REQUIRE(sequencer.getCursor().get() == disruptor::Sequence::INITIAL_VALUE);
}

TEST_CASE("SingleProducerSequencer next should claim sequence", "[sequencer][single]")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    REQUIRE(sequencer.next() == 0);
    REQUIRE(sequencer.next() == 1);
    REQUIRE(sequencer.next() == 2);
}

TEST_CASE("SingleProducerSequencer next(n) should claim batch", "[sequencer][single]")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    long hi = sequencer.next(5);
    REQUIRE(hi == 4);  // 批量结束序列号

    long hi2 = sequencer.next(3);
    REQUIRE(hi2 == 7);
}

TEST_CASE("SingleProducerSequencer next should reject invalid n", "[sequencer][single]")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    REQUIRE_THROWS_AS(sequencer.next(0), std::invalid_argument);
    REQUIRE_THROWS_AS(sequencer.next(-1), std::invalid_argument);
    REQUIRE_THROWS_AS(sequencer.next(bufferSize + 1), std::invalid_argument);
}

TEST_CASE("SingleProducerSequencer publish should update cursor", "[sequencer][single]")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    long seq = sequencer.next();
    REQUIRE(sequencer.getCursor().get() == disruptor::Sequence::INITIAL_VALUE);

    sequencer.publish(seq);
    REQUIRE(sequencer.getCursor().get() == 0);
}

TEST_CASE("SingleProducerSequencer publish(lo, hi) should update cursor to hi", "[sequencer][single]")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    long hi = sequencer.next(5);
    sequencer.publish(0, hi);

    REQUIRE(sequencer.getCursor().get() == hi);
}

TEST_CASE("SingleProducerSequencer tryNext should succeed when space available", "[sequencer][single]")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    long seq = sequencer.tryNext();
    REQUIRE(seq == 0);
}

TEST_CASE("SingleProducerSequencer tryNext should throw when full", "[sequencer][single]")
{
    constexpr int bufferSize = 4;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    disruptor::Sequence gatingSeq(disruptor::Sequence::INITIAL_VALUE);
    sequencer.addGatingSequences({&gatingSeq});

    // 填满缓冲区
    for (int i = 0; i < bufferSize; ++i)
    {
        long seq = sequencer.next();
        sequencer.publish(seq);
    }

    REQUIRE_THROWS_AS(sequencer.tryNext(), disruptor::InsufficientCapacityException);
}

TEST_CASE("SingleProducerSequencer hasAvailableCapacity", "[sequencer][single]")
{
    constexpr int bufferSize = 8;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    REQUIRE(sequencer.hasAvailableCapacity(bufferSize));
    REQUIRE(sequencer.hasAvailableCapacity(1));
}

TEST_CASE("SingleProducerSequencer remainingCapacity", "[sequencer][single]")
{
    constexpr int bufferSize = 8;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    // 添加 gating sequence 来正确测量 remainingCapacity
    disruptor::Sequence gatingSeq(disruptor::Sequence::INITIAL_VALUE);
    sequencer.addGatingSequences({&gatingSeq});

    REQUIRE(sequencer.remainingCapacity() == bufferSize);

    long seq = sequencer.next();
    sequencer.publish(seq);
    // 生产了 1 个事件，消费者还在 -1，剩余 bufferSize - 1
    REQUIRE(sequencer.remainingCapacity() == bufferSize - 1);

    // 消费者消费后，剩余应该恢复
    gatingSeq.set(0);
    REQUIRE(sequencer.remainingCapacity() == bufferSize);
}

TEST_CASE("SingleProducerSequencer isAvailable", "[sequencer][single]")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    // 发布前不可用
    long seq = sequencer.next();
    REQUIRE_FALSE(sequencer.isAvailable(seq));

    sequencer.publish(seq);
    REQUIRE(sequencer.isAvailable(seq));
}

TEST_CASE("SingleProducerSequencer gating sequence management", "[sequencer][single]")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    disruptor::Sequence gatingSeq(0);
    sequencer.addGatingSequences({&gatingSeq});

    REQUIRE(sequencer.removeGatingSequence(&gatingSeq));
    REQUIRE_FALSE(sequencer.removeGatingSequence(&gatingSeq));  // 已移除
}

// ========== MultiProducerSequencerTest ==========
TEST_CASE("MultiProducerSequencer should have correct buffer size", "[sequencer][multi]")
{
    constexpr int bufferSize = 1024;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::MultiProducerSequencer sequencer(bufferSize, waitStrategy);

    REQUIRE(sequencer.getBufferSize() == bufferSize);
}

TEST_CASE("MultiProducerSequencer next should be thread-safe", "[sequencer][multi]")
{
    constexpr int bufferSize = 4096;
    constexpr int numThreads = 4;
    constexpr int numIterations = 1000;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::MultiProducerSequencer sequencer(bufferSize, waitStrategy);

    std::vector<std::thread> threads;
    std::atomic<int> count{0};

    for (int t = 0; t < numThreads; ++t)
    {
        threads.emplace_back([&sequencer, &count] {
            for (int i = 0; i < numIterations; ++i)
            {
                long seq = sequencer.next();
                sequencer.publish(seq);
                count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }

    REQUIRE(count.load() == numThreads * numIterations);
    REQUIRE(sequencer.getCursor().get() == numThreads * numIterations - 1);
}

TEST_CASE("MultiProducerSequencer next(n) should claim batch atomically", "[sequencer][multi]")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::MultiProducerSequencer sequencer(bufferSize, waitStrategy);

    long hi = sequencer.next(5);
    REQUIRE(hi == 4);

    long hi2 = sequencer.next(3);
    REQUIRE(hi2 == 7);
}

TEST_CASE("MultiProducerSequencer tryNext should be thread-safe", "[sequencer][multi]")
{
    constexpr int bufferSize = 1024;
    constexpr int numThreads = 4;
    constexpr int numIterations = 100;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::MultiProducerSequencer sequencer(bufferSize, waitStrategy);

    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};

    for (int t = 0; t < numThreads; ++t)
    {
        threads.emplace_back([&sequencer, &successCount, &failCount] {
            for (int i = 0; i < numIterations; ++i)
            {
                try
                {
                    long seq = sequencer.tryNext();
                    sequencer.publish(seq);
                    successCount.fetch_add(1, std::memory_order_relaxed);
                }
                catch (const disruptor::InsufficientCapacityException&)
                {
                    failCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }

    REQUIRE(successCount.load() + failCount.load() == numThreads * numIterations);
}

TEST_CASE("MultiProducerSequencer isAvailable tracks individual slots", "[sequencer][multi]")
{
    constexpr int bufferSize = 8;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::MultiProducerSequencer sequencer(bufferSize, waitStrategy);

    // 发布 seq 0
    long seq0 = sequencer.next();
    sequencer.publish(seq0);

    // 获取 seq 1 但不发布
    long seq1 = sequencer.next();

    // seq 0 应该可用，seq 1 不应该可用
    REQUIRE(sequencer.isAvailable(0));
    REQUIRE_FALSE(sequencer.isAvailable(1));

    sequencer.publish(seq1);
    REQUIRE(sequencer.isAvailable(1));
}

TEST_CASE("MultiProducerSequencer getHighestPublishedSequence", "[sequencer][multi]")
{
    constexpr int bufferSize = 8;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::MultiProducerSequencer sequencer(bufferSize, waitStrategy);

    // 发布 seq 0, 1, 2
    for (int i = 0; i < 3; ++i)
    {
        long seq = sequencer.next();
        sequencer.publish(seq);
    }

    // 获取 seq 3 但不发布
    sequencer.next();

    // 最高已发布序列应该是 2
    long highest = sequencer.getHighestPublishedSequence(0, 5);
    REQUIRE(highest == 2);
}

TEST_CASE("MultiProducerSequencer batch publish", "[sequencer][multi]")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::MultiProducerSequencer sequencer(bufferSize, waitStrategy);

    long hi = sequencer.next(5);
    long lo = hi - 4;

    REQUIRE(lo == 0);
    REQUIRE(hi == 4);

    sequencer.publish(lo, hi);

    for (long s = lo; s <= hi; ++s)
    {
        REQUIRE(sequencer.isAvailable(s));
    }
}

TEST_CASE("MultiProducerSequencer hasAvailableCapacity", "[sequencer][multi]")
{
    constexpr int bufferSize = 8;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::MultiProducerSequencer sequencer(bufferSize, waitStrategy);

    REQUIRE(sequencer.hasAvailableCapacity(bufferSize));
    REQUIRE(sequencer.hasAvailableCapacity(1));
}

TEST_CASE("MultiProducerSequencer remainingCapacity", "[sequencer][multi]")
{
    constexpr int bufferSize = 8;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::MultiProducerSequencer sequencer(bufferSize, waitStrategy);

    REQUIRE(sequencer.remainingCapacity() == bufferSize);
}

TEST_CASE("MultiProducerSequencer concurrent batch publish", "[sequencer][multi]")
{
    constexpr int bufferSize = 4096;
    constexpr int numThreads = 4;
    constexpr int batchSize = 10;
    constexpr int numIterations = 100;
    disruptor::BlockingWaitStrategy waitStrategy;
    disruptor::MultiProducerSequencer sequencer(bufferSize, waitStrategy);

    std::vector<std::thread> threads;
    std::atomic<long> totalPublished{0};

    for (int t = 0; t < numThreads; ++t)
    {
        threads.emplace_back([&sequencer, &totalPublished] {
            for (int i = 0; i < numIterations; ++i)
            {
                long hi = sequencer.next(batchSize);
                long lo = hi - (batchSize - 1);
                sequencer.publish(lo, hi);
                totalPublished.fetch_add(batchSize, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }

    REQUIRE(totalPublished.load() == numThreads * numIterations * batchSize);
}
