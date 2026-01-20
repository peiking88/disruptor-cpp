#include <atomic>
#include <thread>
#include <vector>

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

TEST_CASE("Multi producer and single consumer")
{
    constexpr int bufferSize = 4096;
    constexpr long eventsPerProducer = 5000;
    constexpr int producers = 2;
    constexpr long totalEvents = eventsPerProducer * producers;

    disruptor::BlockingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<TestEvent>::createMultiProducer(
        [] { return TestEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    CountingHandler handler;
    disruptor::BatchEventProcessor<TestEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumerThread([&] { processor.run(); });

    std::vector<std::thread> producerThreads;
    for (int p = 0; p < producers; ++p)
    {
        producerThreads.emplace_back([&] {
            for (long i = 0; i < eventsPerProducer; ++i)
            {
                long seq = ringBuffer.next();
                ringBuffer.get(seq).value = seq;
                ringBuffer.publish(seq);
            }
        });
    }

    for (auto& t : producerThreads)
    {
        t.join();
    }

    while (handler.getCount() < totalEvents)
    {
        std::this_thread::yield();
    }

    processor.halt();
    consumerThread.join();

    REQUIRE(handler.getCount() == totalEvents);
}
