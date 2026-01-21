#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "disruptor/batch_event_processor.h"
#include "disruptor/event_handler.h"
#include "disruptor/exception_handler.h"
#include "disruptor/ring_buffer.h"
#include "disruptor/wait_strategy.h"

struct ProcessorEvent
{
    long value{0};
};

// BatchEventProcessorTest - 测试批处理事件处理器的循环、生命周期与异常处理功能

// ========== Basic Processing Tests ==========
class SimpleCountingHandler final : public disruptor::EventHandler<ProcessorEvent>
{
public:
    void onEvent(ProcessorEvent&, long, bool) override
    {
        processedCount.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<long> processedCount{0};
};

TEST_CASE("BatchEventProcessor should process events", "[processor]")
{
    constexpr int bufferSize = 64;
    constexpr long events = 100;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ProcessorEvent>::createSingleProducer(
        [] { return ProcessorEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    SimpleCountingHandler handler;
    disruptor::BatchEventProcessor<ProcessorEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumer([&] { processor.run(); });

    for (long i = 0; i < events; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = i;
        ringBuffer.publish(seq);
    }

    while (handler.processedCount.load(std::memory_order_relaxed) < events)
    {
        std::this_thread::yield();
    }

    processor.halt();
    consumer.join();

    REQUIRE(handler.processedCount.load() == events);
}

// ========== Sequence Tracking Tests ==========
class SequenceTrackingHandler final : public disruptor::EventHandler<ProcessorEvent>
{
public:
    void onEvent(ProcessorEvent&, long sequence, bool endOfBatch) override
    {
        lastSequence.store(sequence, std::memory_order_relaxed);
        if (endOfBatch)
        {
            batchEndCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    std::atomic<long> lastSequence{-1};
    std::atomic<long> batchEndCount{0};
};

TEST_CASE("BatchEventProcessor should track sequence correctly", "[processor]")
{
    constexpr int bufferSize = 64;
    constexpr long events = 50;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ProcessorEvent>::createSingleProducer(
        [] { return ProcessorEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    SequenceTrackingHandler handler;
    disruptor::BatchEventProcessor<ProcessorEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumer([&] { processor.run(); });

    for (long i = 0; i < events; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = i;
        ringBuffer.publish(seq);
    }

    while (handler.lastSequence.load(std::memory_order_relaxed) < events - 1)
    {
        std::this_thread::yield();
    }

    processor.halt();
    consumer.join();

    REQUIRE(handler.lastSequence.load() == events - 1);
    REQUIRE(processor.getSequence().get() == events - 1);
}

// ========== Batch Processing Tests ==========
TEST_CASE("BatchEventProcessor should process events in batches", "[processor]")
{
    constexpr int bufferSize = 64;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ProcessorEvent>::createSingleProducer(
        [] { return ProcessorEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    SequenceTrackingHandler handler;
    disruptor::BatchEventProcessor<ProcessorEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumer([&] { processor.run(); });

    // 批量发布
    long hi = ringBuffer.next(10);
    long lo = hi - 9;
    for (long seq = lo; seq <= hi; ++seq)
    {
        ringBuffer.get(seq).value = seq;
    }
    ringBuffer.publish(lo, hi);

    while (handler.lastSequence.load(std::memory_order_relaxed) < hi)
    {
        std::this_thread::yield();
    }

    processor.halt();
    consumer.join();

    REQUIRE(handler.lastSequence.load() == hi);
    // 至少应该有一个 batch 结束
    REQUIRE(handler.batchEndCount.load() >= 1);
}

// ========== Lifecycle Tests ==========
class LifecycleAwareHandler final : public disruptor::EventHandler<ProcessorEvent>
{
public:
    void onEvent(ProcessorEvent&, long, bool) override
    {
        eventCount.fetch_add(1, std::memory_order_relaxed);
    }

    void onStart() override
    {
        startCalled.store(true, std::memory_order_release);
        startTime = std::chrono::steady_clock::now();
    }

    void onShutdown() override
    {
        shutdownCalled.store(true, std::memory_order_release);
        shutdownTime = std::chrono::steady_clock::now();
    }

    std::atomic<bool> startCalled{false};
    std::atomic<bool> shutdownCalled{false};
    std::atomic<long> eventCount{0};
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point shutdownTime;
};

TEST_CASE("BatchEventProcessor should call onStart before processing", "[processor][lifecycle]")
{
    constexpr int bufferSize = 64;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ProcessorEvent>::createSingleProducer(
        [] { return ProcessorEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    LifecycleAwareHandler handler;
    disruptor::BatchEventProcessor<ProcessorEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumer([&] { processor.run(); });

    // 等待 onStart 被调用
    while (!handler.startCalled.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }

    REQUIRE(handler.startCalled.load());

    processor.halt();
    consumer.join();
}

TEST_CASE("BatchEventProcessor should call onShutdown after halt", "[processor][lifecycle]")
{
    constexpr int bufferSize = 64;
    constexpr long events = 10;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ProcessorEvent>::createSingleProducer(
        [] { return ProcessorEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    LifecycleAwareHandler handler;
    disruptor::BatchEventProcessor<ProcessorEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumer([&] { processor.run(); });

    for (long i = 0; i < events; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.publish(seq);
    }

    while (handler.eventCount.load(std::memory_order_relaxed) < events)
    {
        std::this_thread::yield();
    }

    processor.halt();
    consumer.join();

    REQUIRE(handler.startCalled.load());
    REQUIRE(handler.shutdownCalled.load());
    // onStart 应该在 onShutdown 之前
    REQUIRE(handler.startTime < handler.shutdownTime);
}

// ========== isRunning Tests ==========
TEST_CASE("BatchEventProcessor isRunning should reflect state", "[processor]")
{
    constexpr int bufferSize = 64;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ProcessorEvent>::createSingleProducer(
        [] { return ProcessorEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    SimpleCountingHandler handler;
    disruptor::BatchEventProcessor<ProcessorEvent> processor(ringBuffer, barrier, handler);

    REQUIRE_FALSE(processor.isRunning());

    std::thread consumer([&] { processor.run(); });

    // 等待处理器开始运行
    while (!processor.isRunning())
    {
        std::this_thread::yield();
    }
    REQUIRE(processor.isRunning());

    processor.halt();
    consumer.join();

    REQUIRE_FALSE(processor.isRunning());
}

// ========== Exception Handling Tests ==========
class ThrowOnFirstHandler final : public disruptor::EventHandler<ProcessorEvent>
{
public:
    void onEvent(ProcessorEvent&, long sequence, bool) override
    {
        if (sequence == 0)
        {
            throw std::runtime_error("first event error");
        }
        processedCount.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<long> processedCount{0};
};

TEST_CASE("BatchEventProcessor should continue after exception with IgnoreExceptionHandler", "[processor][exception]")
{
    constexpr int bufferSize = 64;
    constexpr long events = 10;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ProcessorEvent>::createSingleProducer(
        [] { return ProcessorEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    ThrowOnFirstHandler handler;
    disruptor::BatchEventProcessor<ProcessorEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    disruptor::IgnoreExceptionHandler<ProcessorEvent> ignoreHandler;
    processor.setExceptionHandler(ignoreHandler);

    std::thread consumer([&] { processor.run(); });

    for (long i = 0; i < events; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.publish(seq);
    }

    // 等待所有事件被处理（第一个抛出异常）
    while (handler.processedCount.load(std::memory_order_relaxed) < events - 1)
    {
        std::this_thread::yield();
    }

    processor.halt();
    consumer.join();

    // 9 个事件成功处理（第一个抛出异常被忽略）
    REQUIRE(handler.processedCount.load() == events - 1);
}

TEST_CASE("BatchEventProcessor should rethrow with FatalExceptionHandler", "[processor][exception]")
{
    constexpr int bufferSize = 64;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ProcessorEvent>::createSingleProducer(
        [] { return ProcessorEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    ThrowOnFirstHandler handler;
    disruptor::BatchEventProcessor<ProcessorEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    disruptor::FatalExceptionHandler<ProcessorEvent> fatalHandler;
    processor.setExceptionHandler(fatalHandler);

    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        long seq = ringBuffer.next();
        ringBuffer.publish(seq);
    });

    REQUIRE_THROWS_AS(processor.run(), std::runtime_error);

    producer.join();
}

// ========== Multiple Consumers Pipeline Tests ==========
class AddValueHandler final : public disruptor::EventHandler<ProcessorEvent>
{
public:
    explicit AddValueHandler(long addValue) : addValue(addValue) {}

    void onEvent(ProcessorEvent& event, long, bool) override
    {
        event.value += addValue;
        processedCount.fetch_add(1, std::memory_order_relaxed);
    }

    long addValue;
    std::atomic<long> processedCount{0};
};

TEST_CASE("BatchEventProcessor should support pipeline topology", "[processor][pipeline]")
{
    constexpr int bufferSize = 64;
    constexpr long events = 50;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ProcessorEvent>::createSingleProducer(
        [] { return ProcessorEvent{}; }, bufferSize, waitStrategy);

    // 第一个处理器
    auto barrier1 = ringBuffer.newBarrier();
    AddValueHandler handler1(10);
    disruptor::BatchEventProcessor<ProcessorEvent> processor1(ringBuffer, barrier1, handler1);

    // 第二个处理器依赖第一个
    auto barrier2 = ringBuffer.newBarrier({&processor1.getSequence()});
    AddValueHandler handler2(100);
    disruptor::BatchEventProcessor<ProcessorEvent> processor2(ringBuffer, barrier2, handler2);

    ringBuffer.addGatingSequences({&processor2.getSequence()});

    std::thread consumer1([&] { processor1.run(); });
    std::thread consumer2([&] { processor2.run(); });

    for (long i = 0; i < events; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = i;
        ringBuffer.publish(seq);
    }

    while (handler2.processedCount.load(std::memory_order_relaxed) < events)
    {
        std::this_thread::yield();
    }

    processor1.halt();
    processor2.halt();
    consumer1.join();
    consumer2.join();

    // 每个事件应该被两个处理器处理
    REQUIRE(handler1.processedCount.load() == events);
    REQUIRE(handler2.processedCount.load() == events);

    // 验证最后一个事件的值
    // 原始值 (events-1) + 10 + 100
    REQUIRE(ringBuffer.get(events - 1).value == (events - 1) + 10 + 100);
}

// ========== Halt Behavior Tests ==========
TEST_CASE("BatchEventProcessor halt should be idempotent", "[processor]")
{
    constexpr int bufferSize = 64;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ProcessorEvent>::createSingleProducer(
        [] { return ProcessorEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    SimpleCountingHandler handler;
    disruptor::BatchEventProcessor<ProcessorEvent> processor(ringBuffer, barrier, handler);

    std::thread consumer([&] { processor.run(); });

    // 等待处理器启动
    while (!processor.isRunning())
    {
        std::this_thread::yield();
    }

    // 多次调用 halt 应该是安全的
    processor.halt();
    processor.halt();
    processor.halt();

    consumer.join();

    REQUIRE_FALSE(processor.isRunning());
}

TEST_CASE("BatchEventProcessor should process remaining events before halt", "[processor]")
{
    constexpr int bufferSize = 64;
    constexpr long events = 20;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ProcessorEvent>::createSingleProducer(
        [] { return ProcessorEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    SimpleCountingHandler handler;
    disruptor::BatchEventProcessor<ProcessorEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    // 先发布所有事件
    for (long i = 0; i < events; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.publish(seq);
    }

    std::thread consumer([&] { processor.run(); });

    // 等待所有事件处理完成
    while (handler.processedCount.load(std::memory_order_relaxed) < events)
    {
        std::this_thread::yield();
    }

    processor.halt();
    consumer.join();

    REQUIRE(handler.processedCount.load() == events);
}
