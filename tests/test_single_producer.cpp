#include <atomic>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "disruptor/batch_event_processor.h"
#include "disruptor/event_handler.h"
#include "disruptor/ring_buffer.h"
#include "disruptor/wait_strategy.h"

struct TestEvent
{
    long value = 0;
};

class CountingHandler final : public disruptor::EventHandler<TestEvent>
{
public:
    void onEvent(TestEvent&, long, bool) override
    {
        count.fetch_add(1, std::memory_order_relaxed);
    }

    long getCount() const
    {
        return count.load(std::memory_order_relaxed);
    }

private:
    std::atomic<long> count{0};
};

TEST_CASE("Single producer and single consumer")
{
    constexpr int bufferSize = 1024;
    constexpr long events = 10000;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<TestEvent>::createSingleProducer(
        [] { return TestEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    CountingHandler handler;
    disruptor::BatchEventProcessor<TestEvent> processor(ringBuffer, barrier, handler);

    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumerThread([&] { processor.run(); });

    for (long i = 0; i < events; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = i;
        ringBuffer.publish(seq);
    }

    while (handler.getCount() < events)
    {
        std::this_thread::yield();
    }

    processor.halt();
    consumerThread.join();

    REQUIRE(handler.getCount() == events);
}
