// OneToThreeDiamondSequencedThroughputTest - 测试 1:3 菱形拓扑吞吐性能
// 拓扑：
//              -> Handler1 (FizzBuzz)
// Producer -> |                        -> Handler3 (聚合结果)
//              -> Handler2 (FizzBuzz)
// Handler1 和 Handler2 并行处理，Handler3 等待两者完成后聚合
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

struct DiamondEvent
{
    long value = 0;
    long fizz = 0;  // 被 3 整除则为 value，否则为 0
    long buzz = 0;  // 被 5 整除则为 value，否则为 0
};

// FizzHandler：检查被 3 整除
class FizzHandler final : public disruptor::EventHandler<DiamondEvent>
{
public:
    void onEvent(DiamondEvent& evt, long, bool) override
    {
        evt.fizz = (evt.value % 3 == 0) ? evt.value : 0;
    }
};

// BuzzHandler：检查被 5 整除
class BuzzHandler final : public disruptor::EventHandler<DiamondEvent>
{
public:
    void onEvent(DiamondEvent& evt, long, bool) override
    {
        evt.buzz = (evt.value % 5 == 0) ? evt.value : 0;
    }
};

// FizzBuzzHandler：聚合 fizz 和 buzz 结果
class FizzBuzzHandler final : public disruptor::EventHandler<DiamondEvent>
{
public:
    void reset(long expected)
    {
        expectedCount = expected;
        count.store(0, std::memory_order_relaxed);
        fizzSum.store(0, std::memory_order_relaxed);
        buzzSum.store(0, std::memory_order_relaxed);
        fizzBuzzSum.store(0, std::memory_order_relaxed);
    }

    void onEvent(DiamondEvent& evt, long, bool) override
    {
        // 如果同时被 3 和 5 整除
        if (evt.fizz != 0 && evt.buzz != 0)
        {
            fizzBuzzSum.fetch_add(evt.value, std::memory_order_relaxed);
        }
        else if (evt.fizz != 0)
        {
            fizzSum.fetch_add(evt.fizz, std::memory_order_relaxed);
        }
        else if (evt.buzz != 0)
        {
            buzzSum.fetch_add(evt.buzz, std::memory_order_relaxed);
        }

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

    long long getFizzSum() const { return fizzSum.load(std::memory_order_relaxed); }
    long long getBuzzSum() const { return buzzSum.load(std::memory_order_relaxed); }
    long long getFizzBuzzSum() const { return fizzBuzzSum.load(std::memory_order_relaxed); }

private:
    std::atomic<long> count{0};
    std::atomic<long long> fizzSum{0};
    std::atomic<long long> buzzSum{0};
    std::atomic<long long> fizzBuzzSum{0};
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
    auto ringBuffer = disruptor::RingBuffer<DiamondEvent>::createSingleProducer(
        [] { return DiamondEvent{}; }, bufferSize, waitStrategy);

    // 创建菱形拓扑
    // Fizz 和 Buzz 并行处理（都直接依赖 RingBuffer cursor）
    auto barrier1 = ringBuffer.newBarrier();
    FizzHandler fizzHandler;
    disruptor::BatchEventProcessor<DiamondEvent> fizzProcessor(ringBuffer, barrier1, fizzHandler);

    auto barrier2 = ringBuffer.newBarrier();
    BuzzHandler buzzHandler;
    disruptor::BatchEventProcessor<DiamondEvent> buzzProcessor(ringBuffer, barrier2, buzzHandler);

    // FizzBuzz 聚合器依赖 Fizz 和 Buzz 两者
    auto barrier3 = ringBuffer.newBarrier({&fizzProcessor.getSequence(), &buzzProcessor.getSequence()});
    FizzBuzzHandler fizzBuzzHandler;
    fizzBuzzHandler.reset(iterations);
    disruptor::BatchEventProcessor<DiamondEvent> fizzBuzzProcessor(ringBuffer, barrier3, fizzBuzzHandler);

    // 只有最终聚合器作为 gating sequence
    ringBuffer.addGatingSequences({&fizzBuzzProcessor.getSequence()});

    // 启动所有消费者
    std::thread fizzThread([&] { fizzProcessor.run(); });
    std::thread buzzThread([&] { buzzProcessor.run(); });
    std::thread fizzBuzzThread([&] { fizzBuzzProcessor.run(); });

    auto start = std::chrono::steady_clock::now();

    // 生产者发布事件
    for (long i = 0; i < iterations; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = i;
        ringBuffer.publish(seq);
    }

    // 等待聚合器完成
    fizzBuzzHandler.waitForExpected();

    auto end = std::chrono::steady_clock::now();

    // 停止所有处理器
    fizzBuzzProcessor.halt();
    fizzProcessor.halt();
    buzzProcessor.halt();
    fizzBuzzThread.join();
    fizzThread.join();
    buzzThread.join();

    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double seconds = nanos / 1'000'000'000.0;
    double opsPerSecond = iterations / seconds;

    std::cout << "PerfTest: OneToThreeDiamondSequencedThroughput\n";
    std::cout << "Diamond: Producer -> [Fizz, Buzz] -> FizzBuzz\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Time(s): " << seconds << "\n";
    std::cout << "Throughput(ops/s): " << opsPerSecond << "\n";
    std::cout << "FizzSum: " << fizzBuzzHandler.getFizzSum() << "\n";
    std::cout << "BuzzSum: " << fizzBuzzHandler.getBuzzSum() << "\n";
    std::cout << "FizzBuzzSum: " << fizzBuzzHandler.getFizzBuzzSum() << "\n";

    return 0;
}
