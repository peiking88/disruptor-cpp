#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <atomic>
#include <chrono>
#include <iostream>
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

/**
 * High-performance handler using FastEventHandler base class.
 */
class ValueAdditionHandler final : public disruptor::FastEventHandler<ValueEvent>
{
protected:
    void processEvent(ValueEvent& evt, long) override
    {
        localSum_ += evt.value;
    }
};

/**
 * Handler with ID support for multi-consumer scenarios.
 */
class ValueAdditionHandlerWithId final : public disruptor::FastEventHandlerWithId<ValueEvent>
{
public:
    explicit ValueAdditionHandlerWithId(int id) : FastEventHandlerWithId(id) {}

protected:
    void processEvent(ValueEvent& evt, long) override
    {
        localSum_ += evt.value;
    }
};

// ============================================================================
// Benchmark: OneToOne (Single Producer, Single Consumer)
// ============================================================================
double benchmarkOneToOne(long iterations, int bufferSize)
{
    disruptor::BusySpinWaitStrategy waitStrategy;
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

    double seconds = std::chrono::duration<double>(end - start).count();
    return iterations / seconds;
}

// ============================================================================
// Benchmark: OneToThree (Single Producer, 3 Consumers Broadcast)
// ============================================================================
double benchmarkOneToThree(long iterations, int bufferSize)
{
    disruptor::BusySpinWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ValueEvent>::createSingleProducer(
        [] { return ValueEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();

    std::vector<std::unique_ptr<ValueAdditionHandlerWithId>> handlers;
    std::vector<std::unique_ptr<disruptor::BatchEventProcessor<ValueEvent>>> processors;
    std::vector<disruptor::Sequence*> gatingSequences;

    for (int i = 0; i < 3; ++i)
    {
        handlers.push_back(std::make_unique<ValueAdditionHandlerWithId>(i));
        handlers.back()->reset(iterations);
        processors.push_back(
            std::make_unique<disruptor::BatchEventProcessor<ValueEvent>>(ringBuffer, barrier, *handlers.back()));
        gatingSequences.push_back(&processors.back()->getSequence());
    }
    ringBuffer.addGatingSequences(gatingSequences);

    std::vector<std::thread> consumerThreads;
    for (auto& processor : processors)
    {
        consumerThreads.emplace_back([&processor] { processor->run(); });
    }

    auto start = std::chrono::steady_clock::now();
    for (long i = 0; i < iterations; ++i)
    {
        long seq = ringBuffer.next();
        ringBuffer.get(seq).value = i;
        ringBuffer.publish(seq);
    }

    for (auto& handler : handlers)
    {
        handler->waitForExpected();
    }
    auto end = std::chrono::steady_clock::now();

    for (auto& processor : processors)
    {
        processor->halt();
    }
    for (auto& t : consumerThreads)
    {
        t.join();
    }

    double seconds = std::chrono::duration<double>(end - start).count();
    return iterations / seconds;
}

// ============================================================================
// Benchmark: ThreeToOne (3 Producers, Single Consumer)
// ============================================================================
double benchmarkThreeToOne(long iterations, int bufferSize, int producers = 3)
{
    disruptor::BusySpinWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ValueEvent>::createMultiProducer(
        [] { return ValueEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();
    ValueAdditionHandler handler;
    handler.reset(iterations);

    disruptor::BatchEventProcessor<ValueEvent> processor(ringBuffer, barrier, handler);
    ringBuffer.addGatingSequences({&processor.getSequence()});

    std::thread consumerThread([&] { processor.run(); });

    long perProducer = iterations / producers;
    long remainder = iterations % producers;

    std::atomic<int> readyCount{0};
    std::atomic<bool> startFlag{false};

    std::vector<std::thread> producerThreads;
    for (int p = 0; p < producers; ++p)
    {
        long quota = perProducer + (p == 0 ? remainder : 0);
        producerThreads.emplace_back([&, quota] {
            readyCount.fetch_add(1, std::memory_order_release);
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

    while (readyCount.load(std::memory_order_acquire) < producers)
    {
        std::this_thread::yield();
    }

    auto start = std::chrono::steady_clock::now();
    startFlag.store(true, std::memory_order_release);

    for (auto& t : producerThreads)
    {
        t.join();
    }

    handler.waitForExpected();
    auto end = std::chrono::steady_clock::now();

    processor.halt();
    consumerThread.join();

    double seconds = std::chrono::duration<double>(end - start).count();
    return iterations / seconds;
}

// ============================================================================
// Benchmark: ThreeToThree (3 Producers, 3 Consumers)
// ============================================================================
double benchmarkThreeToThree(long iterations, int bufferSize, int producers = 3, int consumers = 3)
{
    disruptor::BusySpinWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ValueEvent>::createMultiProducer(
        [] { return ValueEvent{}; }, bufferSize, waitStrategy);

    auto barrier = ringBuffer.newBarrier();

    std::vector<std::unique_ptr<ValueAdditionHandlerWithId>> handlers;
    std::vector<std::unique_ptr<disruptor::BatchEventProcessor<ValueEvent>>> processors;
    std::vector<disruptor::Sequence*> gatingSequences;

    for (int i = 0; i < consumers; ++i)
    {
        handlers.push_back(std::make_unique<ValueAdditionHandlerWithId>(i));
        handlers.back()->reset(iterations);
        processors.push_back(
            std::make_unique<disruptor::BatchEventProcessor<ValueEvent>>(ringBuffer, barrier, *handlers.back()));
        gatingSequences.push_back(&processors.back()->getSequence());
    }
    ringBuffer.addGatingSequences(gatingSequences);

    std::vector<std::thread> consumerThreads;
    for (auto& processor : processors)
    {
        consumerThreads.emplace_back([&processor] { processor->run(); });
    }

    long perProducer = iterations / producers;
    long remainder = iterations % producers;

    std::atomic<int> readyCount{0};
    std::atomic<bool> startFlag{false};

    std::vector<std::thread> producerThreads;
    for (int p = 0; p < producers; ++p)
    {
        long quota = perProducer + (p == 0 ? remainder : 0);
        producerThreads.emplace_back([&, quota] {
            readyCount.fetch_add(1, std::memory_order_release);
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

    while (readyCount.load(std::memory_order_acquire) < producers)
    {
        std::this_thread::yield();
    }

    auto start = std::chrono::steady_clock::now();
    startFlag.store(true, std::memory_order_release);

    for (auto& t : producerThreads)
    {
        t.join();
    }

    for (auto& handler : handlers)
    {
        handler->waitForExpected();
    }
    auto end = std::chrono::steady_clock::now();

    for (auto& processor : processors)
    {
        processor->halt();
    }
    for (auto& t : consumerThreads)
    {
        t.join();
    }

    double seconds = std::chrono::duration<double>(end - start).count();
    return iterations / seconds;
}

// ============================================================================
// Main: Run all benchmarks with nanobench statistics
// ============================================================================
int main()
{
    constexpr long ITERATIONS = 10'000'000L;
    constexpr int BUFFER_SIZE = 1 << 16;
    constexpr int EPOCHS = 11;
    constexpr int WARMUP = 3;

    std::cout << "============================================================\n";
    std::cout << "Disruptor-CPP Performance Benchmark (nanobench statistics)\n";
    std::cout << "============================================================\n";
    std::cout << "Iterations per run: " << ITERATIONS << "\n";
    std::cout << "Buffer size: " << BUFFER_SIZE << "\n";
    std::cout << "Epochs: " << EPOCHS << ", Warmup: " << WARMUP << "\n";
    std::cout << "============================================================\n\n";

    ankerl::nanobench::Bench bench;
    bench.title("Disruptor Throughput")
        .warmup(WARMUP)
        .epochs(EPOCHS)
        .epochIterations(1)     // Each epoch runs the full benchmark once
        .batch(ITERATIONS)      // Report throughput per iteration
        .unit("event")
        .relative(true);

    // Store results for summary
    std::vector<std::pair<std::string, double>> results;

    // OneToOne benchmark
    double oneToOneResult = 0;
    bench.run("OneToOne (1P:1C)", [&] {
        oneToOneResult = benchmarkOneToOne(ITERATIONS, BUFFER_SIZE);
        ankerl::nanobench::doNotOptimizeAway(oneToOneResult);
    });
    results.emplace_back("OneToOne", oneToOneResult);

    // OneToThree benchmark
    double oneToThreeResult = 0;
    bench.run("OneToThree (1P:3C)", [&] {
        oneToThreeResult = benchmarkOneToThree(ITERATIONS, BUFFER_SIZE);
        ankerl::nanobench::doNotOptimizeAway(oneToThreeResult);
    });
    results.emplace_back("OneToThree", oneToThreeResult);

    // ThreeToOne benchmark
    double threeToOneResult = 0;
    bench.run("ThreeToOne (3P:1C)", [&] {
        threeToOneResult = benchmarkThreeToOne(ITERATIONS, BUFFER_SIZE);
        ankerl::nanobench::doNotOptimizeAway(threeToOneResult);
    });
    results.emplace_back("ThreeToOne", threeToOneResult);

    // ThreeToThree benchmark
    double threeToThreeResult = 0;
    bench.run("ThreeToThree (3P:3C)", [&] {
        threeToThreeResult = benchmarkThreeToThree(ITERATIONS, BUFFER_SIZE);
        ankerl::nanobench::doNotOptimizeAway(threeToThreeResult);
    });
    results.emplace_back("ThreeToThree", threeToThreeResult);

    // Print summary with throughput
    std::cout << "\n============================================================\n";
    std::cout << "Final Results Summary\n";
    std::cout << "============================================================\n";
    std::cout << std::fixed;
    for (const auto& [name, throughput] : results)
    {
        std::cout << name << ": " << std::scientific << throughput << " ops/s\n";
    }
    std::cout << "\nNote: nanobench table shows median time per event.\n";
    std::cout << "Throughput = " << ITERATIONS << " / median_time_per_event\n";

    return 0;
}
