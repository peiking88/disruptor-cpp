#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

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

int main()
{
    constexpr int bufferSize = 1 << 16;
    constexpr long events = 1'000'000;

    disruptor::BusySpinWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<TestEvent>::createSingleProducer(
        [] { return TestEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    CountingHandler handler;
    disruptor::BatchEventProcessor<TestEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumerThread([&] { processor.run(); });

    auto start = std::chrono::steady_clock::now();
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
    auto end = std::chrono::steady_clock::now();

    processor.halt();
    consumerThread.join();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    double seconds = duration / 1'000'000.0;
    double throughput = events / seconds;

    std::cout << "Events: " << events << "\n";
    std::cout << "Time(s): " << seconds << "\n";
    std::cout << "Throughput(events/s): " << throughput << "\n";
    return 0;
}
