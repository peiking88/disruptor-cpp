#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <latch>
#include <thread>
#include <vector>

#include "disruptor/batch_event_processor.h"
#include "disruptor/event_handler.h"
#include "disruptor/ring_buffer.h"
#include "disruptor/wait_strategy.h"

struct ValueEvent
{
    long value = 0;
};

/**
 * Optimized handler using FastEventHandler base class.
 */
class ValueAdditionHandler final : public disruptor::FastEventHandler<ValueEvent>
{
protected:
    void processEvent(ValueEvent& evt, long) override
    {
        localSum_ += evt.value;
    }
};

long parseLong(const char* text, long fallback)
{
    if (!text)
    {
        return fallback;
    }
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (end == text)
    {
        return fallback;
    }
    return value;
}

int main(int argc, char** argv)
{
    int producers = static_cast<int>(parseLong(argc > 1 ? argv[1] : nullptr, 3));
    long iterations = parseLong(argc > 2 ? argv[2] : nullptr, 20'000'000L);
    int bufferSize = static_cast<int>(parseLong(argc > 3 ? argv[3] : nullptr, 1 << 16));

    disruptor::BusySpinWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ValueEvent>::createMultiProducer(
        [] { return ValueEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    ValueAdditionHandler handler;
    handler.reset(iterations);

    disruptor::BatchEventProcessor<ValueEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumerThread([&] { processor.run(); });

    long perProducer = iterations / producers;
    long remainder = iterations % producers;

    std::latch ready(static_cast<std::ptrdiff_t>(producers));
    std::atomic<bool> startFlag{false};

    std::vector<std::thread> producerThreads;
    producerThreads.reserve(static_cast<size_t>(producers));

    for (int p = 0; p < producers; ++p)
    {
        long quota = perProducer + (p == 0 ? remainder : 0);
        producerThreads.emplace_back([&, quota] {
            ready.count_down();
            while (!startFlag.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            for (long i = 0; i < quota; ++i)
            {
                long seq = ringBuffer.next();
                ringBuffer.get(seq).value = seq;
                ringBuffer.publish(seq);
            }
        });
    }

    ready.wait();
    auto start = std::chrono::steady_clock::now();
    startFlag.store(true, std::memory_order_release);

    for (auto& t : producerThreads)
    {
        t.join();
    }

    handler.waitForExpected();
    auto end = std::chrono::steady_clock::now();

    processor.halt();
    consumerThread.join();

    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double seconds = nanos / 1'000'000'000.0;
    double opsPerSecond = iterations / seconds;

    std::cout << "PerfTest: ThreeToOneSequencedThroughput\n";
    std::cout << "Producers: " << producers << "\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Time(s): " << seconds << "\n";
    std::cout << "Throughput(ops/s): " << opsPerSecond << "\n";
    return 0;
}
