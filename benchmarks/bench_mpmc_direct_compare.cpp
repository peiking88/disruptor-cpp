// Direct comparison: MPMCQueue vs ConcurrentQueue
// Multi-producer multi-consumer benchmark

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

#include "rigtorp/MPMCQueue.h"
#include "concurrentqueue.h"

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

struct Range {
    long start = 0;
    long count = 0;
};

static std::vector<Range> splitRanges(long total, int producers) {
    std::vector<Range> ranges;
    long per = total / producers;
    long rem = total % producers;
    long cursor = 0;
    for (int p = 0; p < producers; ++p) {
        long cnt = per + (p < rem ? 1 : 0);
        ranges.push_back(Range{cursor, cnt});
        cursor += cnt;
    }
    return ranges;
}

// ==================== MPMCQueue (rigtorp) ====================
Result benchmark_mpmcqueue(int producers, int consumers, long totalMessages, int capacity,
                           const std::vector<int>& consumerCpus, const std::vector<int>& producerCpus) {
    rigtorp::MPMCQueue<long> queue(static_cast<size_t>(capacity));

    auto ranges = splitRanges(totalMessages, producers);

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<long> consumed{0};
    std::atomic<long long> sum{0};

    std::vector<std::thread> consumerThreads;
    consumerThreads.reserve(static_cast<size_t>(consumers));
    for (int c = 0; c < consumers; ++c) {
        consumerThreads.emplace_back([&, c] {
#ifdef __linux__
            setAffinity(consumerCpus[c]);
#endif
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            long long localSum = 0;
            for (;;) {
                if (consumed.load(std::memory_order_acquire) >= totalMessages) {
                    break;
                }
                long v = 0;
                if (queue.try_pop(v)) {
                    localSum += v;
                    long now = consumed.fetch_add(1, std::memory_order_acq_rel) + 1;
                    if (now >= totalMessages) {
                        break;
                    }
                } else {
                    std::this_thread::yield();
                }
            }
            sum.fetch_add(localSum, std::memory_order_relaxed);
        });
    }

    std::vector<std::thread> producerThreads;
    producerThreads.reserve(static_cast<size_t>(producers));
    for (int p = 0; p < producers; ++p) {
        producerThreads.emplace_back([&, p] {
#ifdef __linux__
            setAffinity(producerCpus[p]);
#endif
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            auto r = ranges[static_cast<size_t>(p)];
            for (long i = 0; i < r.count; ++i) {
                queue.push(r.start + i);
            }
        });
    }

    while (ready.load(std::memory_order_acquire) < (producers + consumers)) {
        std::this_thread::yield();
    }

    auto t0 = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);

    for (auto& t : producerThreads) t.join();
    for (auto& t : consumerThreads) t.join();

    auto t1 = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(t1 - t0).count();

    Result r;
    r.seconds = seconds;
    r.opsPerSecond = static_cast<double>(totalMessages) / seconds;
    r.sum = sum.load(std::memory_order_relaxed);
    return r;
}

// ==================== ConcurrentQueue (moodycamel) ====================
Result benchmark_concurrentqueue(int producers, int consumers, long totalMessages,
                                 const std::vector<int>& consumerCpus, const std::vector<int>& producerCpus) {
    moodycamel::ConcurrentQueue<long> queue;

    auto ranges = splitRanges(totalMessages, producers);

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<long> consumed{0};
    std::atomic<long long> sum{0};

    std::vector<std::thread> consumerThreads;
    consumerThreads.reserve(static_cast<size_t>(consumers));
    for (int c = 0; c < consumers; ++c) {
        consumerThreads.emplace_back([&, c] {
#ifdef __linux__
            setAffinity(consumerCpus[c]);
#endif
            moodycamel::ConsumerToken token(queue);

            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            long long localSum = 0;
            for (;;) {
                if (consumed.load(std::memory_order_acquire) >= totalMessages) {
                    break;
                }
                long v = 0;
                if (queue.try_dequeue(token, v)) {
                    localSum += v;
                    long now = consumed.fetch_add(1, std::memory_order_acq_rel) + 1;
                    if (now >= totalMessages) {
                        break;
                    }
                } else {
                    std::this_thread::yield();
                }
            }
            sum.fetch_add(localSum, std::memory_order_relaxed);
        });
    }

    std::vector<std::thread> producerThreads;
    producerThreads.reserve(static_cast<size_t>(producers));
    for (int p = 0; p < producers; ++p) {
        producerThreads.emplace_back([&, p] {
#ifdef __linux__
            setAffinity(producerCpus[p]);
#endif
            moodycamel::ProducerToken token(queue);

            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            auto r = ranges[static_cast<size_t>(p)];
            for (long i = 0; i < r.count; ++i) {
                queue.enqueue(token, r.start + i);
            }
        });
    }

    while (ready.load(std::memory_order_acquire) < (producers + consumers)) {
        std::this_thread::yield();
    }

    auto t0 = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);

    for (auto& t : producerThreads) t.join();
    for (auto& t : consumerThreads) t.join();

    auto t1 = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(t1 - t0).count();

    Result r;
    r.seconds = seconds;
    r.opsPerSecond = static_cast<double>(totalMessages) / seconds;
    r.sum = sum.load(std::memory_order_relaxed);
    return r;
}

} // namespace

int main(int argc, char** argv) {
    int producers = static_cast<int>(parseLong(argc > 1 ? argv[1] : nullptr, 4));
    int consumers = static_cast<int>(parseLong(argc > 2 ? argv[2] : nullptr, 4));
    long totalMessages = parseLong(argc > 3 ? argv[3] : nullptr, 10'000'000L);
    int capacity = static_cast<int>(parseLong(argc > 4 ? argv[4] : nullptr, 1 << 16));
    int baseCpu = static_cast<int>(parseLong(argc > 5 ? argv[5] : nullptr, 0));

    std::cout << "============================================\n";
    std::cout << "MPMC Queue Direct Comparison\n";
    std::cout << "============================================\n";
    std::cout << "Producers:     " << producers << "\n";
    std::cout << "Consumers:     " << consumers << "\n";
    std::cout << "Total messages:" << totalMessages << "\n";
    std::cout << "Queue capacity:" << capacity << "\n";
    std::cout << "Base CPU:      " << baseCpu << "\n\n";

    // Setup CPU mapping
    std::vector<int> consumerCpus(static_cast<size_t>(consumers));
    std::vector<int> producerCpus(static_cast<size_t>(producers));
    for (int i = 0; i < consumers; ++i) consumerCpus[i] = baseCpu + i;
    for (int i = 0; i < producers; ++i) producerCpus[i] = baseCpu + consumers + i;

    // Warmup
    std::cout << "Warming up...\n";
    long warmup = std::min(200'000L, totalMessages);
    (void)benchmark_mpmcqueue(producers, consumers, warmup, capacity, consumerCpus, producerCpus);
    (void)benchmark_concurrentqueue(producers, consumers, warmup, consumerCpus, producerCpus);

    // Benchmark MPMCQueue
    std::cout << "Running MPMCQueue (rigtorp)...\n";
    auto r1 = benchmark_mpmcqueue(producers, consumers, totalMessages, capacity, consumerCpus, producerCpus);

    // Benchmark ConcurrentQueue
    std::cout << "Running ConcurrentQueue (moodycamel)...\n";
    auto r2 = benchmark_concurrentqueue(producers, consumers, totalMessages, consumerCpus, producerCpus);

    // Results
    std::cout << "\n============================================\n";
    std::cout << "Results\n";
    std::cout << "============================================\n\n";

    std::cout << "MPMCQueue (rigtorp):\n";
    std::cout << "  Time:        " << r1.seconds << " s\n";
    std::cout << "  Throughput:  " << r1.opsPerSecond << " msg/s\n";
    std::cout << "  Sum:         " << r1.sum << "\n\n";

    std::cout << "ConcurrentQueue (moodycamel):\n";
    std::cout << "  Time:        " << r2.seconds << " s\n";
    std::cout << "  Throughput:  " << r2.opsPerSecond << " msg/s\n";
    std::cout << "  Sum:         " << r2.sum << "\n\n";

    double ratio = r1.opsPerSecond / r2.opsPerSecond;
    std::cout << "Comparison (MPMCQueue / ConcurrentQueue):\n";
    if (ratio > 1.0) {
        std::cout << "  MPMCQueue is " << ratio << "x faster\n";
    } else {
        std::cout << "  ConcurrentQueue is " << (1.0/ratio) << "x faster\n";
    }
    std::cout << "============================================\n";

    return 0;
}
