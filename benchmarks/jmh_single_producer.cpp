#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

#include "disruptor/batch_event_processor.h"
#include "disruptor/event_handler.h"
#include "disruptor/ring_buffer.h"
#include "disruptor/wait_strategy.h"

struct SimpleEvent
{
    long value = 0;
};

class CountingHandler final : public disruptor::EventHandler<SimpleEvent>
{
public:
    void reset(long expected)
    {
        expectedCount = expected;
        count.store(0, std::memory_order_relaxed);
    }

    void onEvent(SimpleEvent&, long, bool) override
    {
        auto current = count.fetch_add(1, std::memory_order_relaxed) + 1;
        if (current >= expectedCount)
        {
            std::lock_guard<std::mutex> lock(mutex);
            cv.notify_all();
        }
    }

    void waitForExpected()
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return count.load(std::memory_order_relaxed) >= expectedCount; });
    }

private:
    std::atomic<long> count{0};
    long expectedCount{0};
    std::mutex mutex;
    std::condition_variable cv;
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
    long iterations = parseLong(argc > 1 ? argv[1] : nullptr, 10'000'000L);
    int bufferSize = static_cast<int>(parseLong(argc > 2 ? argv[2] : nullptr, 1 << 16));

    disruptor::BusySpinWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<SimpleEvent>::createSingleProducer(
        [] { return SimpleEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    CountingHandler handler;
    handler.reset(iterations);

    disruptor::BatchEventProcessor<SimpleEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumerThread([&] { processor.run(); });

    const long warmup = iterations / 10;
    for (long i = 0; i < warmup; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = i;
        ringBuffer.publish(seq);
    }

    auto start = std::chrono::steady_clock::now();
    for (long i = 0; i < iterations; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = i;
        ringBuffer.publish(seq);
    }
    handler.waitForExpected();
    auto end = std::chrono::steady_clock::now();

    processor.halt();
    consumerThread.join();

    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double seconds = nanos / 1'000'000'000.0;
    double opsPerSecond = iterations / seconds;
    double nsPerOp = static_cast<double>(nanos) / iterations;

    std::cout << "Benchmark: SingleProducerSingleConsumer\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Time(s): " << seconds << "\n";
    std::cout << "Throughput(ops/s): " << opsPerSecond << "\n";
    std::cout << "Average(ns/op): " << nsPerOp << "\n";
    return 0;
}
