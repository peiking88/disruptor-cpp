#include <atomic>
#include <chrono>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "disruptor/exceptions.h"
#include "disruptor/ring_buffer.h"
#include "disruptor/consumer_barrier.h"
#include "disruptor/wait_strategy.h"

struct BarrierEvent
{
    long value{0};
};

// SequenceBarrierTest - 测试序列屏障的等待、警报与中断语义

TEST_CASE("SequenceBarrier should wait for cursor to advance")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<BarrierEvent>::createSingleProducer(
        [] { return BarrierEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();

    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = 42;
        ringBuffer.publish(seq);
    });

    long available = barrier.waitFor(0);
    REQUIRE(available >= 0);
    REQUIRE(ringBuffer.get(0).value == 42);

    producer.join();
}

TEST_CASE("SequenceBarrier should throw AlertException when alerted")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<BarrierEvent>::createSingleProducer(
        [] { return BarrierEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();

    std::thread alerter([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        barrier.alert();
    });

    REQUIRE_THROWS_AS(barrier.waitFor(0), disruptor::AlertException);

    alerter.join();
}

TEST_CASE("SequenceBarrier alert state should be checkable")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<BarrierEvent>::createSingleProducer(
        [] { return BarrierEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();

    REQUIRE_FALSE(barrier.isAlerted());

    barrier.alert();
    REQUIRE(barrier.isAlerted());

    barrier.clearAlert();
    REQUIRE_FALSE(barrier.isAlerted());
}

TEST_CASE("SequenceBarrier should return current cursor")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<BarrierEvent>::createSingleProducer(
        [] { return BarrierEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();

    REQUIRE(barrier.getCursor() == disruptor::Sequence::INITIAL_VALUE);

    long seq = ringBuffer.next();
    ringBuffer.publish(seq);

    REQUIRE(barrier.getCursor() == 0);
}

TEST_CASE("SequenceBarrier should wait for dependent sequences")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<BarrierEvent>::createSingleProducer(
        [] { return BarrierEvent{}; }, bufferSize, waitStrategy);

    // 创建一个依赖序列
    disruptor::Sequence dependent(disruptor::Sequence::INITIAL_VALUE);
    auto barrier = ringBuffer.newBarrier({&dependent});

    // 发布事件
    for (int i = 0; i < 5; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.publish(seq);
    }

    // 在另一个线程中推进依赖序列
    std::thread advancer([&dependent] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        dependent.set(2);
    });

    // 等待应该阻塞直到 dependent 达到 2
    long available = barrier.waitFor(2);
    REQUIRE(available >= 2);

    advancer.join();
}

TEST_CASE("SequenceBarrier should handle multiple dependents")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<BarrierEvent>::createSingleProducer(
        [] { return BarrierEvent{}; }, bufferSize, waitStrategy);

    // 发布一些事件
    for (int i = 0; i < 10; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.publish(seq);
    }

    // 创建多个依赖序列
    disruptor::Sequence dep1(5);
    disruptor::Sequence dep2(3);  // 最小值
    disruptor::Sequence dep3(7);
    auto barrier = ringBuffer.newBarrier({&dep1, &dep2, &dep3});

    // waitFor 应该返回不超过最小依赖序列的值
    long available = barrier.waitFor(3);
    REQUIRE(available == 3);  // 受限于 dep2
}

TEST_CASE("SequenceBarrier clearAlert should allow retry")
{
    constexpr int bufferSize = 64;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<BarrierEvent>::createSingleProducer(
        [] { return BarrierEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();

    // 设置警报
    barrier.alert();

    // 第一次 waitFor 应该抛出异常
    REQUIRE_THROWS_AS(barrier.waitFor(0), disruptor::AlertException);

    // 清除警报
    barrier.clearAlert();

    // 在另一线程发布事件
    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        long seq = ringBuffer.next();
        ringBuffer.publish(seq);
    });

    // 现在应该可以等待
    long available = barrier.waitFor(0);
    REQUIRE(available >= 0);

    producer.join();
}

TEST_CASE("SequenceBarrier should support signaling from multiple threads")
{
    constexpr int bufferSize = 64;
    constexpr int numThreads = 4;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<BarrierEvent>::createSingleProducer(
        [] { return BarrierEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    std::atomic<int> alertCount{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([&barrier, &alertCount] {
            barrier.alert();
            ++alertCount;
        });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    REQUIRE(barrier.isAlerted());
    REQUIRE(alertCount == numThreads);
}

TEST_CASE("SequenceBarrier with YieldingWaitStrategy")
{
    constexpr int bufferSize = 64;
    disruptor::YieldingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<BarrierEvent>::createSingleProducer(
        [] { return BarrierEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();

    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = 100;
        ringBuffer.publish(seq);
    });

    long available = barrier.waitFor(0);
    REQUIRE(available >= 0);
    REQUIRE(ringBuffer.get(0).value == 100);

    producer.join();
}
