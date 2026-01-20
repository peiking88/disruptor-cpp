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

struct ValueEvent
{
    long value = 0;
};

class ValueAdditionHandler final : public disruptor::EventHandler<ValueEvent>
{
public:
    void reset(long expected)
    {
        expectedCount = expected;
        count.store(0, std::memory_order_relaxed);
        sum.store(0, std::memory_order_relaxed);
    }

    void onEvent(ValueEvent& evt, long, bool) override
    {
        sum.fetch_add(evt.value, std::memory_order_relaxed);
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

    long long getSum() const
    {
        return sum.load(std::memory_order_relaxed);
    }

private:
    std::atomic<long> count{0};
    std::atomic<long long> sum{0};
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

    disruptor::YieldingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ValueEvent>::createSingleProducer(
        [] { return ValueEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    ValueAdditionHandler handler;
    handler.reset(iterations);

    disruptor::BatchEventProcessor<ValueEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumerThread([&] { processor.run(); });

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

    long long expectedSum = (static_cast<long long>(iterations - 1) * iterations) / 2;

    std::cout << "PerfTest: OneToOneSequencedThroughput\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Time(s): " << seconds << "\n";
    std::cout << "Throughput(ops/s): " << opsPerSecond << "\n";
    std::cout << "Sum: " << handler.getSum() << " (expected " << expectedSum << ")\n";
    return 0;
}
