// OneToThreePipelineSequencedThroughputTest - 测试 1:3 管线拓扑吞吐性能
// 拓扑：Producer -> Handler1 -> Handler2 -> Handler3 (串行管线)
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

struct PipelineEvent
{
    long value = 0;
    long stage1Result = 0;
    long stage2Result = 0;
    long stage3Result = 0;
};

// 第一阶段处理器：value * 2
class Stage1Handler final : public disruptor::EventHandler<PipelineEvent>
{
public:
    void onEvent(PipelineEvent& evt, long, bool) override
    {
        evt.stage1Result = evt.value * 2;
    }
};

// 第二阶段处理器：stage1Result + 10
class Stage2Handler final : public disruptor::EventHandler<PipelineEvent>
{
public:
    void onEvent(PipelineEvent& evt, long, bool) override
    {
        evt.stage2Result = evt.stage1Result + 10;
    }
};

// 第三阶段处理器：stage2Result * 3
class Stage3Handler final : public disruptor::EventHandler<PipelineEvent>
{
public:
    void reset(long expected)
    {
        expectedCount = expected;
        count.store(0, std::memory_order_relaxed);
        sum.store(0, std::memory_order_relaxed);
    }

    void onEvent(PipelineEvent& evt, long, bool) override
    {
        evt.stage3Result = evt.stage2Result * 3;
        sum.fetch_add(evt.stage3Result, std::memory_order_relaxed);
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
    auto ringBuffer = disruptor::RingBuffer<PipelineEvent>::createSingleProducer(
        [] { return PipelineEvent{}; }, bufferSize, waitStrategy);

    // 创建三阶段管线
    // Stage1 直接依赖 RingBuffer cursor
    auto barrier1 = ringBuffer.newBarrier();
    Stage1Handler handler1;
    disruptor::BatchEventProcessor<PipelineEvent> processor1(ringBuffer, barrier1, handler1);

    // Stage2 依赖 Stage1 的序列
    auto barrier2 = ringBuffer.newBarrier({&processor1.getSequence()});
    Stage2Handler handler2;
    disruptor::BatchEventProcessor<PipelineEvent> processor2(ringBuffer, barrier2, handler2);

    // Stage3 依赖 Stage2 的序列
    auto barrier3 = ringBuffer.newBarrier({&processor2.getSequence()});
    Stage3Handler handler3;
    handler3.reset(iterations);
    disruptor::BatchEventProcessor<PipelineEvent> processor3(ringBuffer, barrier3, handler3);

    // 只有最后一个阶段作为 gating sequence
    ringBuffer.addGatingSequences({&processor3.getSequence()});

    // 启动所有消费者（按管线顺序启动）
    std::thread consumer1([&] { processor1.run(); });
    std::thread consumer2([&] { processor2.run(); });
    std::thread consumer3([&] { processor3.run(); });

    auto start = std::chrono::steady_clock::now();

    // 生产者发布事件
    for (long i = 0; i < iterations; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = i;
        ringBuffer.publish(seq);
    }

    // 等待最后阶段完成
    handler3.waitForExpected();

    auto end = std::chrono::steady_clock::now();

    // 停止所有处理器（逆序停止）
    processor3.halt();
    processor2.halt();
    processor1.halt();
    consumer3.join();
    consumer2.join();
    consumer1.join();

    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double seconds = nanos / 1'000'000'000.0;
    double opsPerSecond = iterations / seconds;

    // 计算预期结果：sum of ((i * 2 + 10) * 3) for i in 0..iterations-1
    // = sum of (6*i + 30) = 6 * sum(i) + 30 * iterations
    // = 6 * (iterations-1)*iterations/2 + 30*iterations
    long long expectedSum = 6LL * ((iterations - 1) * iterations / 2) + 30LL * iterations;

    std::cout << "PerfTest: OneToThreePipelineSequencedThroughput\n";
    std::cout << "Pipeline: Producer -> Stage1(*2) -> Stage2(+10) -> Stage3(*3)\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Time(s): " << seconds << "\n";
    std::cout << "Throughput(ops/s): " << opsPerSecond << "\n";
    std::cout << "Sum: " << handler3.getSum() << " (expected " << expectedSum << ")\n";

    return 0;
}
