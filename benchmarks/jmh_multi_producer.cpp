#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

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
    int producers = static_cast<int>(parseLong(argc > 1 ? argv[1] : nullptr, 4));
    long iterations = parseLong(argc > 2 ? argv[2] : nullptr, 10'000'000L);
    int bufferSize = static_cast<int>(parseLong(argc > 3 ? argv[3] : nullptr, 1 << 22));
    int batchSize = static_cast<int>(parseLong(argc > 4 ? argv[4] : nullptr, 100));
    bool useBatch = argc > 5;

    disruptor::BusySpinWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<SimpleEvent>::createMultiProducer(
        [] { return SimpleEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    CountingHandler handler;
    handler.reset(iterations * producers);

    disruptor::BatchEventProcessor<SimpleEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumerThread([&] { processor.run(); });

    std::vector<std::thread> producerThreads;
    producerThreads.reserve(static_cast<size_t>(producers));

    auto start = std::chrono::steady_clock::now();
    for (int p = 0; p < producers; ++p)
    {
        producerThreads.emplace_back([&, p] {
            if (useBatch)
            {
                long remaining = iterations;
                while (remaining > 0)
                {
                    int chunk = static_cast<int>(std::min<long>(remaining, batchSize));
                    long hi = ringBuffer.next(chunk);
                    long lo = hi - (chunk - 1);
                    for (long seq = lo; seq <= hi; ++seq)
                    {
                        ringBuffer.get(seq).value = seq;
                    }
                    ringBuffer.publish(lo, hi);
                    remaining -= chunk;
                }
            }
            else
            {
                for (long i = 0; i < iterations; ++i)
                {
                    long seq = ringBuffer.next();
                    ringBuffer.get(seq).value = seq;
                    ringBuffer.publish(seq);
                }
            }
        });
    }

    for (auto& t : producerThreads)
    {
        t.join();
    }

    handler.waitForExpected();
    auto end = std::chrono::steady_clock::now();

    processor.halt();
    consumerThread.join();

    long totalOps = iterations * producers;
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double seconds = nanos / 1'000'000'000.0;
    double opsPerSecond = totalOps / seconds;

    std::cout << "Benchmark: MultiProducerSingleConsumer\n";
    std::cout << "Mode: " << (useBatch ? "batch" : "single") << "\n";
    std::cout << "Producers: " << producers << "\n";
    std::cout << "Iterations per producer: " << iterations << "\n";
    std::cout << "Time(s): " << seconds << "\n";
    std::cout << "Throughput(ops/s): " << opsPerSecond << "\n";
    return 0;
}
