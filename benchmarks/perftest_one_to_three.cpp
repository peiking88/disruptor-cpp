// OneToThreeSequencedThroughputTest - 测试 1:3 广播吞吐性能
// 拓扑：1 个生产者 -> 3 个并行消费者（广播模式，每个消费者独立处理所有事件）
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

struct ValueEvent
{
    long value = 0;
};

class ValueAdditionHandler final : public disruptor::EventHandler<ValueEvent>
{
public:
    explicit ValueAdditionHandler(int id) : handlerId(id) {}

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

    int getId() const { return handlerId; }

private:
    int handlerId;
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
    constexpr int numConsumers = 3;

    disruptor::YieldingWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ValueEvent>::createSingleProducer(
        [] { return ValueEvent{}; }, bufferSize, waitStrategy);

    // 创建 3 个并行消费者（广播模式）
    std::vector<std::unique_ptr<ValueAdditionHandler>> handlers;
    std::vector<disruptor::SequenceBarrier> barriers;
    std::vector<std::unique_ptr<disruptor::BatchEventProcessor<ValueEvent>>> processors;
    std::vector<disruptor::Sequence*> gatingSequences;

    for (int i = 0; i < numConsumers; ++i)
    {
        handlers.push_back(std::make_unique<ValueAdditionHandler>(i));
        handlers.back()->reset(iterations);
        barriers.push_back(ringBuffer.newBarrier());
    }

    for (int i = 0; i < numConsumers; ++i)
    {
        processors.push_back(std::make_unique<disruptor::BatchEventProcessor<ValueEvent>>(
            ringBuffer, barriers[i], *handlers[i]));
        gatingSequences.push_back(&processors.back()->getSequence());
    }

    ringBuffer.addGatingSequences(gatingSequences);

    // 启动所有消费者
    std::vector<std::thread> consumerThreads;
    for (int i = 0; i < numConsumers; ++i)
    {
        consumerThreads.emplace_back([&, i] { processors[i]->run(); });
    }

    auto start = std::chrono::steady_clock::now();

    // 生产者发布事件
    for (long i = 0; i < iterations; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = i;
        ringBuffer.publish(seq);
    }

    // 等待所有消费者完成
    for (auto& handler : handlers)
    {
        handler->waitForExpected();
    }

    auto end = std::chrono::steady_clock::now();

    // 停止所有处理器
    for (auto& processor : processors)
    {
        processor->halt();
    }
    for (auto& thread : consumerThreads)
    {
        thread.join();
    }

    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double seconds = nanos / 1'000'000'000.0;
    double opsPerSecond = iterations / seconds;

    long long expectedSum = (static_cast<long long>(iterations - 1) * iterations) / 2;

    std::cout << "PerfTest: OneToThreeSequencedThroughput (Broadcast)\n";
    std::cout << "Consumers: " << numConsumers << "\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Time(s): " << seconds << "\n";
    std::cout << "Throughput(ops/s): " << opsPerSecond << "\n";

    for (const auto& handler : handlers)
    {
        std::cout << "Consumer " << handler->getId() << " Sum: " << handler->getSum()
                  << " (expected " << expectedSum << ")\n";
    }

    return 0;
}
