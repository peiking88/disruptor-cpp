// Direct comparison: SPSCQueue vs ReaderWriterQueue
// Single-producer single-consumer benchmark

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

#include "rigtorp/SPSCQueue.h"
#include "readerwriterqueue.h"

namespace {

long parseLong(const char* text, long fallback) {
    if (!text) return fallback;
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    return (end == text) ? fallback : value;
}

#ifdef __linux__
static bool setAffinity(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) == 0;
}
#endif

struct Result {
    double seconds = 0.0;
    double opsPerSecond = 0.0;
    long long sum = 0;
};

// ==================== SPSCQueue (rigtorp) ====================
Result benchmark_spscqueue(long iterations, int capacity, int consumerCpu, int producerCpu) {
    rigtorp::SPSCQueue<long> queue(static_cast<size_t>(capacity));

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<long long> sum{0};

    std::thread consumer([&] {
#ifdef __linux__
        setAffinity(consumerCpu);
#endif
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        long long localSum = 0;
        long received = 0;
        while (received < iterations) {
            auto* item = queue.front();
            if (item) {
                localSum += *item;
                queue.pop();
                ++received;
            } else {
                std::this_thread::yield();
            }
        }
        sum.store(localSum, std::memory_order_relaxed);
    });

    std::thread producer([&] {
#ifdef __linux__
        setAffinity(producerCpu);
#endif
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (long i = 0; i < iterations; ++i) {
            queue.push(i);
        }
    });

    while (ready.load(std::memory_order_acquire) < 2) {
        std::this_thread::yield();
    }

    auto t0 = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);

    producer.join();
    consumer.join();

    auto t1 = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(t1 - t0).count();

    Result r;
    r.seconds = seconds;
    r.opsPerSecond = static_cast<double>(iterations) / seconds;
    r.sum = sum.load(std::memory_order_relaxed);
    return r;
}

// ==================== ReaderWriterQueue (moodycamel) ====================
Result benchmark_readerwriterqueue(long iterations, int capacity, int consumerCpu, int producerCpu) {
    moodycamel::ReaderWriterQueue<long> queue(static_cast<size_t>(capacity));

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<long long> sum{0};

    std::thread consumer([&] {
#ifdef __linux__
        setAffinity(consumerCpu);
#endif
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        long long localSum = 0;
        long received = 0;
        long v = 0;
        while (received < iterations) {
            if (queue.try_dequeue(v)) {
                localSum += v;
                ++received;
            } else {
                std::this_thread::yield();
            }
        }
        sum.store(localSum, std::memory_order_relaxed);
    });

    std::thread producer([&] {
#ifdef __linux__
        setAffinity(producerCpu);
#endif
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (long i = 0; i < iterations; ++i) {
            while (!queue.try_enqueue(i)) {
                std::this_thread::yield();
            }
        }
    });

    while (ready.load(std::memory_order_acquire) < 2) {
        std::this_thread::yield();
    }

    auto t0 = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);

    producer.join();
    consumer.join();

    auto t1 = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(t1 - t0).count();

    Result r;
    r.seconds = seconds;
    r.opsPerSecond = static_cast<double>(iterations) / seconds;
    r.sum = sum.load(std::memory_order_relaxed);
    return r;
}

} // namespace

int main(int argc, char** argv) {
    long iterations = parseLong(argc > 1 ? argv[1] : nullptr, 10'000'000L);
    int capacity = static_cast<int>(parseLong(argc > 2 ? argv[2] : nullptr, 1 << 16));
    int consumerCpu = static_cast<int>(parseLong(argc > 3 ? argv[3] : nullptr, 0));
    int producerCpu = static_cast<int>(parseLong(argc > 4 ? argv[4] : nullptr, 1));

    std::cout << "============================================\n";
    std::cout << "SPSC Queue Direct Comparison\n";
    std::cout << "============================================\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Queue capacity: " << capacity << "\n";
#ifdef __linux__
    std::cout << "CPU affinity: consumer=" << consumerCpu << ", producer=" << producerCpu << "\n";
#endif
    std::cout << "\n";

    // Warmup
    std::cout << "Warming up...\n";
    (void)benchmark_spscqueue(100'000L, capacity, consumerCpu, producerCpu);
    (void)benchmark_readerwriterqueue(100'000L, capacity, consumerCpu, producerCpu);

    // Benchmark SPSCQueue
    std::cout << "Running SPSCQueue (rigtorp)...\n";
    auto r1 = benchmark_spscqueue(iterations, capacity, consumerCpu, producerCpu);

    // Benchmark ReaderWriterQueue
    std::cout << "Running ReaderWriterQueue (moodycamel)...\n";
    auto r2 = benchmark_readerwriterqueue(iterations, capacity, consumerCpu, producerCpu);

    // Results
    std::cout << "\n============================================\n";
    std::cout << "Results\n";
    std::cout << "============================================\n\n";

    std::cout << "SPSCQueue (rigtorp):\n";
    std::cout << "  Time:        " << r1.seconds << " s\n";
    std::cout << "  Throughput:  " << r1.opsPerSecond << " msg/s\n";
    std::cout << "  Sum:         " << r1.sum << "\n\n";

    std::cout << "ReaderWriterQueue (moodycamel):\n";
    std::cout << "  Time:        " << r2.seconds << " s\n";
    std::cout << "  Throughput:  " << r2.opsPerSecond << " msg/s\n";
    std::cout << "  Sum:         " << r2.sum << "\n\n";

    double ratio = r1.opsPerSecond / r2.opsPerSecond;
    std::cout << "Comparison (SPSCQueue / ReaderWriterQueue):\n";
    if (ratio > 1.0) {
        std::cout << "  SPSCQueue is " << ratio << "x faster\n";
    } else {
        std::cout << "  ReaderWriterQueue is " << (1.0/ratio) << "x faster\n";
    }
    std::cout << "============================================\n";

    return 0;
}
