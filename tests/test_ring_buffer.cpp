#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "disruptor/ring_buffer.h"
#include "disruptor/wait_strategy.h"

struct TestEvent
{
    long value{0};
};

// RingBufferTest - 测试 RingBuffer 的容量、发布、覆盖与读写边界功能

TEST_CASE("RingBuffer should have correct buffer size")
{
    constexpr int bufferSize = 1024;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<TestEvent>::createSingleProducer(
        [] { return TestEvent{}; }, bufferSize, waitStrategy);

    REQUIRE(ringBuffer.getBufferSize() == bufferSize);
}

TEST_CASE("RingBuffer should claimAndGetPreallocated")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<TestEvent>::createSingleProducer(
        [] { return TestEvent{}; }, bufferSize, waitStrategy);

    long seq = ringBuffer.next();
    REQUIRE(seq == 0);

    TestEvent& event = ringBuffer.get(seq);
    event.value = 42;
    ringBuffer.publish(seq);

    REQUIRE(ringBuffer.get(0).value == 42);
}

TEST_CASE("RingBuffer should publish multiple events and get them")
{
    constexpr int bufferSize = 64;
    constexpr long numEvents = 10;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<TestEvent>::createSingleProducer(
        [] { return TestEvent{}; }, bufferSize, waitStrategy);

    for (long i = 0; i < numEvents; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = i * 100;
        ringBuffer.publish(seq);
    }

    for (long i = 0; i < numEvents; ++i)
    {
        REQUIRE(ringBuffer.get(i).value == i * 100);
    }
}

TEST_CASE("RingBuffer should wrap around when buffer is full")
{
    constexpr int bufferSize = 4;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<TestEvent>::createSingleProducer(
        [] { return TestEvent{}; }, bufferSize, waitStrategy);

    // 模拟消费者
    disruptor::Sequence gatingSeq(disruptor::Sequence::INITIAL_VALUE);
    ringBuffer.addGatingSequences({&gatingSeq});

    // 发布 bufferSize 个事件
    for (int i = 0; i < bufferSize; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = i;
        ringBuffer.publish(seq);
    }

    // 消费者消费第一个事件
    gatingSeq.set(0);

    // 应该可以发布下一个事件（wrap around）
    long nextSeq = ringBuffer.next();
    REQUIRE(nextSeq == bufferSize);

    // 由于 wrap，索引 0 的槽位被覆盖
    ringBuffer.get(nextSeq).value = 999;
    ringBuffer.publish(nextSeq);
    REQUIRE(ringBuffer.get(nextSeq).value == 999);
}

TEST_CASE("RingBuffer should track cursor position")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<TestEvent>::createSingleProducer(
        [] { return TestEvent{}; }, bufferSize, waitStrategy);

    REQUIRE(ringBuffer.getCursor() == disruptor::Sequence::INITIAL_VALUE);

    long seq = ringBuffer.next();
    REQUIRE(ringBuffer.getCursor() == disruptor::Sequence::INITIAL_VALUE);

    ringBuffer.publish(seq);
    REQUIRE(ringBuffer.getCursor() == 0);
}

TEST_CASE("RingBuffer should create barrier with dependents")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<TestEvent>::createSingleProducer(
        [] { return TestEvent{}; }, bufferSize, waitStrategy);

    disruptor::Sequence dep1(5);
    disruptor::Sequence dep2(10);
    auto barrier = ringBuffer.newBarrier({&dep1, &dep2});

    REQUIRE(barrier.getCursor() == disruptor::Sequence::INITIAL_VALUE);
}

TEST_CASE("RingBuffer should support batch publishing")
{
    constexpr int bufferSize = 64;
    constexpr int batchSize = 5;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<TestEvent>::createSingleProducer(
        [] { return TestEvent{}; }, bufferSize, waitStrategy);

    long hi = ringBuffer.next(batchSize);
    long lo = hi - (batchSize - 1);

    REQUIRE(lo == 0);
    REQUIRE(hi == batchSize - 1);

    for (long seq = lo; seq <= hi; ++seq)
    {
        ringBuffer.get(seq).value = seq * 10;
    }

    ringBuffer.publish(lo, hi);
    REQUIRE(ringBuffer.getCursor() == hi);
}

TEST_CASE("RingBuffer tryNext should succeed when space available")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<TestEvent>::createSingleProducer(
        [] { return TestEvent{}; }, bufferSize, waitStrategy);

    long seq = ringBuffer.tryNext();
    REQUIRE(seq == 0);
    ringBuffer.publish(seq);
}

TEST_CASE("RingBuffer tryNext should throw when no space")
{
    constexpr int bufferSize = 4;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<TestEvent>::createSingleProducer(
        [] { return TestEvent{}; }, bufferSize, waitStrategy);

    disruptor::Sequence gatingSeq(disruptor::Sequence::INITIAL_VALUE);
    ringBuffer.addGatingSequences({&gatingSeq});

    // 填满缓冲区
    for (int i = 0; i < bufferSize; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.publish(seq);
    }

    // 应该抛出异常
    REQUIRE_THROWS_AS(ringBuffer.tryNext(), disruptor::InsufficientCapacityException);
}

TEST_CASE("RingBuffer should add and remove gating sequences")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<TestEvent>::createSingleProducer(
        [] { return TestEvent{}; }, bufferSize, waitStrategy);

    disruptor::Sequence gatingSeq(0);
    ringBuffer.addGatingSequences({&gatingSeq});

    bool removed = ringBuffer.removeGatingSequence(&gatingSeq);
    REQUIRE(removed);

    // 移除不存在的序列应返回 false
    bool removedAgain = ringBuffer.removeGatingSequence(&gatingSeq);
    REQUIRE_FALSE(removedAgain);
}

TEST_CASE("MultiProducer RingBuffer should handle concurrent publishing")
{
    constexpr int bufferSize = 1024;
    constexpr long eventsPerProducer = 100;
    constexpr int numProducers = 4;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<TestEvent>::createMultiProducer(
        [] { return TestEvent{}; }, bufferSize, waitStrategy);

    std::vector<std::thread> producers;
    for (int p = 0; p < numProducers; ++p)
    {
        producers.emplace_back([&ringBuffer, p] {
            for (long i = 0; i < eventsPerProducer; ++i)
            {
                long seq = ringBuffer.next();
                ringBuffer.get(seq).value = p * 1000 + i;
                ringBuffer.publish(seq);
            }
        });
    }

    for (auto& t : producers)
    {
        t.join();
    }

    REQUIRE(ringBuffer.getCursor() == numProducers * eventsPerProducer - 1);
}
