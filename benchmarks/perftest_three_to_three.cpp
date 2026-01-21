// ThreeToThreeSequencedThroughputTest - 测试 3:3 吞吐性能
// 拓扑：3 个生产者 -> 3 个消费者（广播模式）
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
    int numProducers = static_cast<int>(parseLong(argc > 1 ? argv[1] : nullptr, 3));
    int numConsumers = static_cast<int>(parseLong(argc > 2 ? argv[2] : nullptr, 3));
    long iterations = parseLong(argc > 3 ? argv[3] : nullptr, 10'000'000L);
    int bufferSize = static_cast<int>(parseLong(argc > 4 ? argv[4] : nullptr, 1 << 16));

    disruptor::BusySpinWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ValueEvent>::createMultiProducer(
        [] { return ValueEvent{}; }, bufferSize, waitStrategy);

    // 创建消费者（广播模式）
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

    // 准备生产者同步启动
    long perProducer = iterations / numProducers;
    long remainder = iterations % numProducers;

    std::atomic<int> readyCount{0};
    std::mutex readyMutex;
    std::condition_variable readyCv;
    std::atomic<bool> startFlag{false};

    std::vector<std::thread> producerThreads;
    producerThreads.reserve(static_cast<size_t>(numProducers));

    for (int p = 0; p < numProducers; ++p)
    {
        long quota = perProducer + (p == 0 ? remainder : 0);
        producerThreads.emplace_back([&, quota] {
            {
                std::lock_guard<std::mutex> lock(readyMutex);
                readyCount.fetch_add(1, std::memory_order_relaxed);
            }
            readyCv.notify_one();
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

    // 等待所有生产者就绪
    {
        std::unique_lock<std::mutex> lock(readyMutex);
        readyCv.wait(lock, [&] { return readyCount.load(std::memory_order_relaxed) >= numProducers; });
    }
    auto start = std::chrono::steady_clock::now();
    startFlag.store(true, std::memory_order_release);

    // 等待所有生产者完成
    for (auto& t : producerThreads)
    {
        t.join();
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

    std::cout << "PerfTest: ThreeToThreeSequencedThroughput\n";
    std::cout << "Producers: " << numProducers << "\n";
    std::cout << "Consumers: " << numConsumers << "\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Time(s): " << seconds << "\n";
    std::cout << "Throughput(ops/s): " << opsPerSecond << "\n";

    for (const auto& handler : handlers)
    {
        std::cout << "Consumer " << handler->getId() << " Sum: " << handler->getSum() << "\n";
    }

    return 0;
}
