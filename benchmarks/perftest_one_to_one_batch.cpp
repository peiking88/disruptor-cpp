// OneToOneSequencedBatchThroughputTest - 测试 1:1 批量吞吐性能
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

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
 * Eliminates atomic operations in hot path.
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
    long iterations = parseLong(argc > 1 ? argv[1] : nullptr, 10'000'000L);
    int bufferSize = static_cast<int>(parseLong(argc > 2 ? argv[2] : nullptr, 1 << 16));
    int batchSize = static_cast<int>(parseLong(argc > 3 ? argv[3] : nullptr, 10));

    disruptor::BusySpinWaitStrategy waitStrategy;  // BusySpinWaitStrategy for maximum throughput
    auto ringBuffer = disruptor::RingBuffer<ValueEvent>::createSingleProducer(
        [] { return ValueEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    ValueAdditionHandler handler;
    handler.reset(iterations);

    disruptor::BatchEventProcessor<ValueEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumerThread([&] { processor.run(); });

    auto start = std::chrono::steady_clock::now();

    long remaining = iterations;
    long valueCounter = 0;
    while (remaining > 0)
    {
        int chunk = static_cast<int>(std::min<long>(remaining, batchSize));
        long hi = ringBuffer.next(chunk);
        long lo = hi - (chunk - 1);
        for (long seq = lo; seq <= hi; ++seq)
        {
            ringBuffer.get(seq).value = valueCounter++;
        }
        ringBuffer.publish(lo, hi);
        remaining -= chunk;
    }

    handler.waitForExpected();
    auto end = std::chrono::steady_clock::now();

    processor.halt();
    consumerThread.join();

    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double seconds = nanos / 1'000'000'000.0;
    double opsPerSecond = iterations / seconds;

    long long expectedSum = (static_cast<long long>(iterations - 1) * iterations) / 2;

    std::cout << "PerfTest: OneToOneSequencedBatchThroughput\n";
    std::cout << "BatchSize: " << batchSize << "\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Time(s): " << seconds << "\n";
    std::cout << "Throughput(ops/s): " << opsPerSecond << "\n";
    std::cout << "Sum: " << handler.getSum() << " (expected " << expectedSum << ")\n";
    return 0;
}
