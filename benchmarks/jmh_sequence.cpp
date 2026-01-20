#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>

#include "disruptor/sequence.h"

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

template <typename Fn>
long long runBenchmark(const char* name, long iterations, Fn&& fn)
{
    auto start = std::chrono::steady_clock::now();
    for (long i = 0; i < iterations; ++i)
    {
        fn();
    }
    auto end = std::chrono::steady_clock::now();
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double nsPerOp = static_cast<double>(nanos) / iterations;
    double opsPerSecond = iterations / (nanos / 1'000'000'000.0);
    std::cout << name << ": " << nsPerOp << " ns/op, " << opsPerSecond << " ops/s\n";
    return nanos;
}

int main(int argc, char** argv)
{
    long iterations = parseLong(argc > 1 ? argv[1] : nullptr, 50'000'000L);

    std::atomic<long> atomicValue{0};
    disruptor::Sequence sequence{0};

    long sink = 0;

    std::cout << "Benchmark: Sequence vs std::atomic\n";
    std::cout << "Iterations: " << iterations << "\n";

    runBenchmark("Atomic get", iterations, [&] { sink += atomicValue.load(std::memory_order_relaxed); });
    runBenchmark("Atomic set", iterations, [&] { atomicValue.store(1, std::memory_order_relaxed); });
    runBenchmark("Atomic getAndAdd", iterations, [&] { sink += atomicValue.fetch_add(1, std::memory_order_relaxed); });

    runBenchmark("Sequence get", iterations, [&] { sink += sequence.get(); });
    runBenchmark("Sequence set", iterations, [&] { sequence.set(1); });
    runBenchmark("Sequence incrementAndGet", iterations, [&] { sink += sequence.incrementAndGet(); });

    std::cout << "Sink: " << sink << "\n";
    return 0;
}
