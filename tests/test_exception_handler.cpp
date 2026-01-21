#include <atomic>
#include <thread>
#include <chrono>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "disruptor/batch_event_processor.h"
#include "disruptor/event_handler.h"
#include "disruptor/exception_handler.h"
#include "disruptor/ring_buffer.h"
#include "disruptor/wait_strategy.h"

struct ExceptionEvent
{
    long value{0};
};

class ThrowingHandler final : public disruptor::EventHandler<ExceptionEvent>
{
public:
    void onEvent(ExceptionEvent&, long, bool) override
    {
        throw std::runtime_error("boom");
    }
};

class LifecycleHandler final : public disruptor::EventHandler<ExceptionEvent>
{
public:
    void onEvent(ExceptionEvent&, long, bool) override
    {
        processed.fetch_add(1, std::memory_order_relaxed);
    }
    void onStart() override
    {
        started.store(true, std::memory_order_relaxed);
    }
    void onShutdown() override
    {
        shut.store(true, std::memory_order_relaxed);
    }

    std::atomic<bool> started{false};
    std::atomic<bool> shut{false};
    std::atomic<long> processed{0};
};

TEST_CASE("FatalExceptionHandler rethrows on handler exception")
{
    constexpr int bufferSize = 1024;
    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ExceptionEvent>::createSingleProducer(
        [] { return ExceptionEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    ThrowingHandler handler;
    disruptor::BatchEventProcessor<ExceptionEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    disruptor::FatalExceptionHandler<ExceptionEvent> fatalHandler;
    processor.setExceptionHandler(fatalHandler);

    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = 42;
        ringBuffer.publish(seq);
    });

    REQUIRE_THROWS_AS(processor.run(), std::runtime_error);
    producer.join();
}

TEST_CASE("Lifecycle callbacks are invoked")
{
    constexpr int bufferSize = 1024;
    constexpr long events = 1000;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ExceptionEvent>::createSingleProducer(
        [] { return ExceptionEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    LifecycleHandler handler;
    disruptor::BatchEventProcessor<ExceptionEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumer([&] { processor.run(); });

    for (long i = 0; i < events; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = i;
        ringBuffer.publish(seq);
    }

    while (handler.processed.load(std::memory_order_relaxed) < events)
    {
        std::this_thread::yield();
    }

    processor.halt();
    consumer.join();

    REQUIRE(handler.started.load(std::memory_order_relaxed));
    REQUIRE(handler.shut.load(std::memory_order_relaxed));
    REQUIRE(handler.processed.load(std::memory_order_relaxed) == events);
}
