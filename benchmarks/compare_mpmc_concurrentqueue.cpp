#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#endif

#include "disruptor/consumer_barrier.h"
#include "disruptor/ring_buffer.h"
#include "disruptor/sequence.h"
#include "disruptor/wait_strategy.h"
#include "disruptor/work_handler.h"
#include "disruptor/work_processor.h"

#include "concurrentqueue.h"

namespace {

struct ValueEvent {
    long value = 0;
};

long parseLong(const char* text, long fallback) {
    if (!text) {
        return fallback;
    }
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (end == text) {
        return fallback;
    }
    return value;
}

struct CpuInfo {
    int cpu = -1;
    int package = 0;
    int core = 0;
    int node = 0;
};

#ifdef __linux__
static bool readIntFile(const std::string& path, int& out) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    in >> out;
    return static_cast<bool>(in);
}

static bool readOnline(int cpu) {
    if (cpu == 0) {
        return true;
    }
    int v = 1;
    if (!readIntFile("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/online", v)) {
        return true;
    }
    return v != 0;
}

static int detectNode(int cpu) {
    std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/";
    DIR* d = opendir(base.c_str());
    if (!d) {
        return 0;
    }
    int node = 0;
    for (dirent* e = readdir(d); e != nullptr; e = readdir(d)) {
        std::string name = e->d_name;
        if (name.rfind("node", 0) == 0 && name.size() > 4) {
            node = std::atoi(name.c_str() + 4);
            break;
        }
    }
    closedir(d);
    return node;
}

static std::vector<CpuInfo> enumerateCpus() {
    std::vector<CpuInfo> out;

    DIR* d = opendir("/sys/devices/system/cpu");
    if (!d) {
        return out;
    }

    for (dirent* e = readdir(d); e != nullptr; e = readdir(d)) {
        std::string name = e->d_name;
        if (name.rfind("cpu", 0) != 0 || name.size() <= 3) {
            continue;
        }
        bool allDigits = true;
        for (size_t i = 3; i < name.size(); ++i) {
            if (name[i] < '0' || name[i] > '9') {
                allDigits = false;
                break;
            }
        }
        if (!allDigits) {
            continue;
        }

        int cpu = std::atoi(name.c_str() + 3);
        if (!readOnline(cpu)) {
            continue;
        }

        CpuInfo info;
        info.cpu = cpu;
        (void)readIntFile("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/physical_package_id", info.package);
        (void)readIntFile("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/core_id", info.core);
        info.node = detectNode(cpu);
        out.push_back(info);
    }

    closedir(d);
    return out;
}

static bool setAffinityStrict(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    if (rc != 0) {
        std::cerr << "pthread_setaffinity_np(cpu=" << cpu << ") failed: " << rc << "\n";
        return false;
    }
    return true;
}

static bool cpuExistsOnline(const std::vector<CpuInfo>& cpus, int cpu) {
    for (auto const& c : cpus) {
        if (c.cpu == cpu) {
            return true;
        }
    }
    return false;
}

static std::vector<int> selectDistinctPhysicalCpusSameNode(const std::vector<CpuInfo>& cpus, int baseCpu, int needed) {
    int baseNode = 0;
    int basePkg = 0;
    for (auto const& c : cpus) {
        if (c.cpu == baseCpu) {
            baseNode = c.node;
            basePkg = c.package;
            break;
        }
    }

    std::vector<CpuInfo> sameNode;
    for (auto const& c : cpus) {
        if (c.node == baseNode) {
            sameNode.push_back(c);
        }
    }

    auto coreKey = [](CpuInfo const& c) {
        return (static_cast<long long>(c.package) << 32) | static_cast<unsigned>(c.core);
    };

    std::vector<int> selected;
    selected.reserve(static_cast<size_t>(needed));
    std::vector<long long> usedCores;
    usedCores.reserve(sameNode.size());

    auto tryAdd = [&](CpuInfo const& c) {
        long long key = coreKey(c);
        for (auto k : usedCores) {
            if (k == key) {
                return;
            }
        }
        usedCores.push_back(key);
        selected.push_back(c.cpu);
    };

    // Add base first if present.
    for (auto const& c : sameNode) {
        if (c.cpu == baseCpu) {
            tryAdd(c);
            break;
        }
    }

    // Prefer same package, then any within node.
    for (int pass = 0; pass < 2 && static_cast<int>(selected.size()) < needed; ++pass) {
        for (auto const& c : sameNode) {
            if (static_cast<int>(selected.size()) >= needed) {
                break;
            }
            if (c.cpu == baseCpu) {
                continue;
            }
            if (pass == 0 && c.package != basePkg) {
                continue;
            }
            tryAdd(c);
        }
    }

    // If still not enough, allow SMT siblings in node.
    if (static_cast<int>(selected.size()) < needed) {
        for (auto const& c : sameNode) {
            if (static_cast<int>(selected.size()) >= needed) {
                break;
            }
            bool already = false;
            for (auto s : selected) {
                if (s == c.cpu) {
                    already = true;
                    break;
                }
            }
            if (!already) {
                selected.push_back(c.cpu);
            }
        }
    }

    return selected;
}
#endif

struct Range {
    long start = 0;
    long count = 0;
};

static std::vector<Range> splitRanges(long totalMessages, int producers) {
    std::vector<Range> ranges;
    ranges.reserve(static_cast<size_t>(producers));

    long per = totalMessages / producers;
    long rem = totalMessages % producers;

    long cursor = 0;
    for (int p = 0; p < producers; ++p) {
        long cnt = per + (p < rem ? 1 : 0);
        ranges.push_back(Range{cursor, cnt});
        cursor += cnt;
    }
    return ranges;
}

struct RunResult {
    double seconds = 0.0;
    double opsPerSecond = 0.0;
    long long sum = 0;
};

struct SumWorkHandler final : public disruptor::WorkHandler<ValueEvent>
{
    void onEvent(ValueEvent& event, long) override
    {
        sum += event.value;
    }

    long long sum = 0;
};

RunResult run_disruptor_mpmc(int producers, int consumers, long totalMessages, int bufferSize, int workBatchSize, int publishBatch,
                            const std::vector<int>& cpuMapConsumers, const std::vector<int>& cpuMapProducers) {
    disruptor::BusySpinWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ValueEvent>::createMultiProducer(
        [] { return ValueEvent{}; }, bufferSize, waitStrategy);

    disruptor::Sequence workSequence{disruptor::Sequence::INITIAL_VALUE};

    std::vector<std::unique_ptr<SumWorkHandler>> handlers;
    std::vector<std::unique_ptr<disruptor::WorkProcessor<ValueEvent>>> processors;
    std::vector<disruptor::Sequence*> gating;

    handlers.reserve(static_cast<size_t>(consumers));
    processors.reserve(static_cast<size_t>(consumers));
    gating.reserve(static_cast<size_t>(consumers));

    for (int i = 0; i < consumers; ++i)
    {
        handlers.emplace_back(std::make_unique<SumWorkHandler>());
        processors.emplace_back(std::make_unique<disruptor::WorkProcessor<ValueEvent>>(
            ringBuffer, ringBuffer.newBarrier(), *handlers.back(), workSequence, totalMessages - 1, workBatchSize));
        gating.push_back(&processors.back()->getSequence());
    }

    ringBuffer.addGatingSequences(gating);

    auto ranges = splitRanges(totalMessages, producers);

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};

    std::vector<std::thread> consumerThreads;
    consumerThreads.reserve(static_cast<size_t>(consumers));
    for (int c = 0; c < consumers; ++c)
    {
        consumerThreads.emplace_back([&, c] {
#ifdef __linux__
            if (!setAffinityStrict(cpuMapConsumers[static_cast<size_t>(c)]))
            {
                std::exit(3);
            }
#endif
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            processors[static_cast<size_t>(c)]->run();
        });
    }

    std::vector<std::thread> producerThreads;
    producerThreads.reserve(static_cast<size_t>(producers));
    for (int p = 0; p < producers; ++p)
    {
        producerThreads.emplace_back([&, p] {
#ifdef __linux__
            if (!setAffinityStrict(cpuMapProducers[static_cast<size_t>(p)]))
            {
                std::exit(3);
            }
#endif
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            auto r = ranges[static_cast<size_t>(p)];

            // Batch publish to reduce sequencer+signal overhead.
            int batch = publishBatch;
            if (batch < 1)
            {
                batch = 1;
            }
            if (batch > bufferSize)
            {
                batch = bufferSize;
            }

            long produced = 0;
            while (produced < r.count)
            {
                int n = static_cast<int>(std::min<long>(batch, r.count - produced));
                long hi = ringBuffer.next(n);
                long lo = hi - n + 1;
                for (int i = 0; i < n; ++i)
                {
                    ringBuffer.get(lo + i).value = r.start + produced + i;
                }
                ringBuffer.publish(lo, hi);
                produced += n;
            }
        });
    }

    while (ready.load(std::memory_order_acquire) < (producers + consumers))
    {
        std::this_thread::yield();
    }

    auto t0 = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);

    for (auto& t : producerThreads)
    {
        t.join();
    }
    for (auto& t : consumerThreads)
    {
        t.join();
    }
    auto t1 = std::chrono::steady_clock::now();

    long long sum = 0;
    for (auto& h : handlers)
    {
        sum += h->sum;
    }

    double seconds = std::chrono::duration<double>(t1 - t0).count();
    RunResult r;
    r.seconds = seconds;
    r.opsPerSecond = static_cast<double>(totalMessages) / seconds;
    r.sum = sum;
    return r;
}

RunResult run_concurrentqueue_mpmc(int producers, int consumers, long totalMessages, const std::vector<int>& cpuMapConsumers,
                                  const std::vector<int>& cpuMapProducers) {
    moodycamel::ConcurrentQueue<long> q;

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
            if (!setAffinityStrict(cpuMapConsumers[static_cast<size_t>(c)])) {
                std::exit(3);
            }
#endif
            moodycamel::ConsumerToken token(q);

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
                if (q.try_dequeue(token, v)) {
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
            if (!setAffinityStrict(cpuMapProducers[static_cast<size_t>(p)])) {
                std::exit(3);
            }
#endif
            moodycamel::ProducerToken token(q);

            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            auto r = ranges[static_cast<size_t>(p)];
            for (long i = 0; i < r.count; ++i) {
                q.enqueue(token, r.start + i);
            }
        });
    }

    while (ready.load(std::memory_order_acquire) < (producers + consumers)) {
        std::this_thread::yield();
    }

    auto t0 = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);

    for (auto& t : producerThreads) {
        t.join();
    }
    for (auto& t : consumerThreads) {
        t.join();
    }
    auto t1 = std::chrono::steady_clock::now();

    double seconds = std::chrono::duration<double>(t1 - t0).count();
    RunResult r;
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
    int bufferSize = static_cast<int>(parseLong(argc > 4 ? argv[4] : nullptr, 1 << 16));
    int baseCpu = static_cast<int>(parseLong(argc > 5 ? argv[5] : nullptr, 0));
    int workBatchSize = static_cast<int>(parseLong(argc > 6 ? argv[6] : nullptr, 8));
    int publishBatch = static_cast<int>(parseLong(argc > 7 ? argv[7] : nullptr, 1024));

#ifdef __linux__
    auto cpus = enumerateCpus();
    if (cpus.empty()) {
        std::cerr << "Failed to enumerate CPUs from sysfs; cannot do strict pinning.\n";
        return 2;
    }
    if (!cpuExistsOnline(cpus, baseCpu)) {
        std::cerr << "baseCpu not online/exists: " << baseCpu << "\n";
        return 2;
    }

    int needed = producers + consumers;
    auto picked = selectDistinctPhysicalCpusSameNode(cpus, baseCpu, needed);
    if (static_cast<int>(picked.size()) < needed) {
        std::cerr << "Not enough CPUs to pin MPMC (need " << needed << ").\n";
        return 2;
    }

    std::vector<int> cpuMapConsumers;
    std::vector<int> cpuMapProducers;
    cpuMapConsumers.reserve(static_cast<size_t>(consumers));
    cpuMapProducers.reserve(static_cast<size_t>(producers));

    for (int i = 0; i < consumers; ++i) {
        cpuMapConsumers.push_back(picked[static_cast<size_t>(i)]);
    }
    for (int i = 0; i < producers; ++i) {
        cpuMapProducers.push_back(picked[static_cast<size_t>(consumers + i)]);
    }

    std::cout << "Benchmark: MPMC (each message consumed once)\n";
    std::cout << "Disruptor-CPP consumer model: WorkProcessor (batch claim) work-queue\n";
    std::cout << "ConcurrentQueue consumer model: try_dequeue work-queue\n";
    std::cout << "Producers: " << producers << "\n";
    std::cout << "Consumers: " << consumers << "\n";
    std::cout << "Total messages: " << totalMessages << "\n";
    std::cout << "RingBuffer size (disruptor): " << bufferSize << "\n";
    std::cout << "WorkProcessor claim batch: " << workBatchSize << "\n";
    std::cout << "Producer publish batch: " << publishBatch << "\n";
    std::cout << "Pin mode: numa-local + physical-core-stride (strict)\n";
    std::cout << "Pinning: consumers ->";
    for (auto c : cpuMapConsumers) {
        std::cout << " CPU" << c;
    }
    std::cout << ", producers ->";
    for (auto p : cpuMapProducers) {
        std::cout << " CPU" << p;
    }
    std::cout << "\n\n";

    long warmupMessages = std::min<long>(200'000L, totalMessages);
    (void)run_disruptor_mpmc(producers, consumers, warmupMessages, bufferSize, workBatchSize, publishBatch, cpuMapConsumers, cpuMapProducers);
    (void)run_concurrentqueue_mpmc(producers, consumers, warmupMessages, cpuMapConsumers, cpuMapProducers);

    auto d = run_disruptor_mpmc(producers, consumers, totalMessages, bufferSize, workBatchSize, publishBatch, cpuMapConsumers, cpuMapProducers);
    auto q = run_concurrentqueue_mpmc(producers, consumers, totalMessages, cpuMapConsumers, cpuMapProducers);
#else
    std::cout << "Benchmark: MPMC (each message consumed once)\n";
    std::cout << "Disruptor-CPP consumer model: WorkProcessor (batch claim) work-queue\n";
    std::cout << "ConcurrentQueue consumer model: try_dequeue work-queue\n";
    std::cout << "Producers: " << producers << "\n";
    std::cout << "Consumers: " << consumers << "\n";
    std::cout << "Total messages: " << totalMessages << "\n";
    std::cout << "RingBuffer size (disruptor): " << bufferSize << "\n\n";

    std::vector<int> cpuMapConsumers(static_cast<size_t>(consumers), 0);
    std::vector<int> cpuMapProducers(static_cast<size_t>(producers), 0);

    long warmupMessages = std::min<long>(200'000L, totalMessages);
    (void)run_disruptor_mpmc(producers, consumers, warmupMessages, bufferSize, workBatchSize, publishBatch, cpuMapConsumers, cpuMapProducers);
    (void)run_concurrentqueue_mpmc(producers, consumers, warmupMessages, cpuMapConsumers, cpuMapProducers);

    auto d = run_disruptor_mpmc(producers, consumers, totalMessages, bufferSize, workBatchSize, publishBatch, cpuMapConsumers, cpuMapProducers);
    auto q = run_concurrentqueue_mpmc(producers, consumers, totalMessages, cpuMapConsumers, cpuMapProducers);
#endif

    std::cout << "Disruptor-CPP:\n";
    std::cout << "  Time(s): " << d.seconds << "\n";
    std::cout << "  Throughput(msg/s): " << d.opsPerSecond << "\n";
    std::cout << "  Sum: " << d.sum << "\n\n";

    std::cout << "moodycamel::ConcurrentQueue:\n";
    std::cout << "  Time(s): " << q.seconds << "\n";
    std::cout << "  Throughput(msg/s): " << q.opsPerSecond << "\n";
    std::cout << "  Sum: " << q.sum << "\n\n";

    std::cout << "Speedup (Disruptor / ConcurrentQueue): " << (d.opsPerSecond / q.opsPerSecond) << "x\n";
    return 0;
}
