// PingPongSequencedLatencyTest - 测试乒乓往返延迟性能
// 两个处理器互相发送事件，测量往返延迟
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "disruptor/batch_event_processor.h"
#include "disruptor/event_handler.h"
#include "disruptor/ring_buffer.h"
#include "disruptor/wait_strategy.h"

struct PingPongEvent
{
    long value = 0;
};

// Pinger: 发送 ping，等待 pong
class PingerHandler final : public disruptor::EventHandler<PingPongEvent>
{
public:
    PingerHandler(disruptor::RingBuffer<PingPongEvent>& pongBuffer, long iterations)
        : pongBuffer(pongBuffer), totalIterations(iterations)
    {
        latencies.reserve(static_cast<size_t>(iterations));
    }

    void onEvent(PingPongEvent& evt, long, bool) override
    {
        // 收到 pong，记录延迟
        auto end = std::chrono::steady_clock::now();
        auto start = std::chrono::steady_clock::time_point(
            std::chrono::nanoseconds(evt.value));
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        latencies.push_back(latency);

        if (static_cast<long>(latencies.size()) < totalIterations)
        {
            // 发送下一个 ping
            long seq = pongBuffer.next();
            pongBuffer.get(seq).value = std::chrono::steady_clock::now().time_since_epoch().count();
            pongBuffer.publish(seq);
        }
        else
        {
            done.store(true, std::memory_order_release);
        }
    }

    void startPing()
    {
        // 发送第一个 ping
        long seq = pongBuffer.next();
        pongBuffer.get(seq).value = std::chrono::steady_clock::now().time_since_epoch().count();
        pongBuffer.publish(seq);
    }

    bool isDone() const
    {
        return done.load(std::memory_order_acquire);
    }

    const std::vector<long long>& getLatencies() const { return latencies; }

private:
    disruptor::RingBuffer<PingPongEvent>& pongBuffer;
    long totalIterations;
    std::vector<long long> latencies;
    std::atomic<bool> done{false};
};

// Ponger: 收到 ping，返回 pong
class PongerHandler final : public disruptor::EventHandler<PingPongEvent>
{
public:
    explicit PongerHandler(disruptor::RingBuffer<PingPongEvent>& pingBuffer)
        : pingBuffer(pingBuffer)
    {
    }

    void onEvent(PingPongEvent& evt, long, bool) override
    {
        // 收到 ping，立即返回 pong（保持相同的时间戳）
        long seq = pingBuffer.next();
        pingBuffer.get(seq).value = evt.value;
        pingBuffer.publish(seq);
    }

private:
    disruptor::RingBuffer<PingPongEvent>& pingBuffer;
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
    long iterations = parseLong(argc > 1 ? argv[1] : nullptr, 1'000'000L);
    int bufferSize = static_cast<int>(parseLong(argc > 2 ? argv[2] : nullptr, 1024));
    std::string wait = (argc > 3 && argv[3]) ? std::string(argv[3]) : std::string("busy");

    disruptor::BusySpinWaitStrategy busyPing;
    disruptor::BusySpinWaitStrategy busyPong;
    disruptor::YieldingWaitStrategy yieldingPing;
    disruptor::YieldingWaitStrategy yieldingPong;

    disruptor::WaitStrategy& pingWaitStrategy = (wait == "yield" || wait == "yielding")
        ? static_cast<disruptor::WaitStrategy&>(yieldingPing)
        : static_cast<disruptor::WaitStrategy&>(busyPing);

    disruptor::WaitStrategy& pongWaitStrategy = (wait == "yield" || wait == "yielding")
        ? static_cast<disruptor::WaitStrategy&>(yieldingPong)
        : static_cast<disruptor::WaitStrategy&>(busyPong);

    auto pingBuffer = disruptor::RingBuffer<PingPongEvent>::createSingleProducer(
        [] { return PingPongEvent{}; }, bufferSize, pingWaitStrategy);

    auto pongBuffer = disruptor::RingBuffer<PingPongEvent>::createSingleProducer(
        [] { return PingPongEvent{}; }, bufferSize, pongWaitStrategy);

    // 创建处理器
    auto pingBarrier = pingBuffer.newBarrier();
    PingerHandler pinger(pongBuffer, iterations);
    disruptor::BatchEventProcessor<PingPongEvent> pingerProcessor(pingBuffer, pingBarrier, pinger);
    pingBuffer.addGatingSequences({&pingerProcessor.getSequence()});

    auto pongBarrier = pongBuffer.newBarrier();
    PongerHandler ponger(pingBuffer);
    disruptor::BatchEventProcessor<PingPongEvent> pongerProcessor(pongBuffer, pongBarrier, ponger);
    pongBuffer.addGatingSequences({&pongerProcessor.getSequence()});

    // 启动处理器
    std::thread pingerThread([&] { pingerProcessor.run(); });
    std::thread pongerThread([&] { pongerProcessor.run(); });

    // 等待处理器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto start = std::chrono::steady_clock::now();

    // 发送第一个 ping
    pinger.startPing();

    // 等待完成
    while (!pinger.isDone())
    {
        std::this_thread::yield();
    }

    auto end = std::chrono::steady_clock::now();

    // 停止处理器
    pingerProcessor.halt();
    pongerProcessor.halt();
    pingerThread.join();
    pongerThread.join();

    // 计算统计数据
    auto latencies = pinger.getLatencies();
    std::sort(latencies.begin(), latencies.end());

    auto totalNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double totalSeconds = totalNanos / 1'000'000'000.0;

    long long minLatency = latencies.front();
    long long maxLatency = latencies.back();
    long long p50 = latencies[latencies.size() / 2];
    long long p90 = latencies[static_cast<size_t>(latencies.size() * 0.9)];
    long long p99 = latencies[static_cast<size_t>(latencies.size() * 0.99)];
    long long p999 = latencies[static_cast<size_t>(latencies.size() * 0.999)];

    long long sum = 0;
    for (auto lat : latencies)
    {
        sum += lat;
    }
    double avgLatency = static_cast<double>(sum) / latencies.size();

    std::cout << "PerfTest: PingPongSequencedLatency\n";
    std::cout << "WaitStrategy: " << ((wait == "yield" || wait == "yielding") ? "Yielding" : "BusySpin") << "\n";
    std::cout << "BufferSize: " << bufferSize << "\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Total Time(s): " << totalSeconds << "\n";
    std::cout << "Throughput(round-trips/s): " << iterations / totalSeconds << "\n";
    std::cout << "\nLatency Statistics (ns):\n";
    std::cout << "  Min:    " << minLatency << "\n";
    std::cout << "  Avg:    " << std::fixed << std::setprecision(2) << avgLatency << "\n";
    std::cout << "  P50:    " << p50 << "\n";
    std::cout << "  P90:    " << p90 << "\n";
    std::cout << "  P99:    " << p99 << "\n";
    std::cout << "  P99.9:  " << p999 << "\n";
    std::cout << "  Max:    " << maxLatency << "\n";

    return 0;
}
