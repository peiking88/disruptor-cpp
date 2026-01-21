#include <atomic>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "disruptor/batch_event_processor.h"
#include "disruptor/event_handler.h"
#include "disruptor/ring_buffer.h"
#include "disruptor/wait_strategy.h"

struct StressEvent
{
    long value{0};
};

class StressHandler final : public disruptor::EventHandler<StressEvent>
{
public:
    void onEvent(StressEvent&, long, bool) override
    {
        processed.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<long> processed{0};
};

TEST_CASE("Stress multi-producer single-consumer throughput")
{
    constexpr int bufferSize = 8192;
    constexpr int producers = 4;
    constexpr long eventsPerProducer = 20000;
    constexpr long total = eventsPerProducer * producers;

    disruptor::YieldingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<StressEvent>::createMultiProducer(
        [] { return StressEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    StressHandler handler;
    disruptor::BatchEventProcessor<StressEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumer([&] { processor.run(); });

    std::vector<std::thread> producerThreads;
    producerThreads.reserve(producers);
    for (int p = 0; p < producers; ++p)
    {
        producerThreads.emplace_back([&] {
            for (long i = 0; i < eventsPerProducer; ++i)
            {
                long seq = ringBuffer.next();
                ringBuffer.get(seq).value = i;
                ringBuffer.publish(seq);
            }
        });
    }

    for (auto& t : producerThreads)
    {
        t.join();
    }

    while (handler.processed.load(std::memory_order_relaxed) < total)
    {
        std::this_thread::yield();
    }

    processor.halt();
    consumer.join();

    REQUIRE(handler.processed.load(std::memory_order_relaxed) == total);
}
