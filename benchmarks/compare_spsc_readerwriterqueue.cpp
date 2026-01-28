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
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#endif

#include "disruptor/consumer_barrier.h"
#include "disruptor/ring_buffer.h"
#include "disruptor/sequence.h"
#include "disruptor/wait_strategy.h"

#include "readerwriterqueue.h"

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
    // best-effort: find /sys/devices/system/cpu/cpuX/nodeY
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
    // Prefer: same node as baseCpu, distinct (package,core). Fall back to SMT siblings if needed.
    int baseNode = 0;
    int basePkg = 0;
    int baseCore = 0;
    for (auto const& c : cpus) {
        if (c.cpu == baseCpu) {
            baseNode = c.node;
            basePkg = c.package;
            baseCore = c.core;
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

    // First pass: choose one CPU per physical core.
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

    // Ensure baseCpu first if possible.
    for (auto const& c : sameNode) {
        if (c.cpu == baseCpu) {
            tryAdd(c);
            break;
        }
    }

    // Then pick other cores, prefer same package, then by CPU id.
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

    // Second pass: if still not enough, allow SMT siblings (any remaining CPUs in node).
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

struct RunResult {
    double seconds = 0.0;
    double opsPerSecond = 0.0;
    long long sum = 0;
};

RunResult run_disruptor_spsc(long iterations, int bufferSize, int consumerCpu, int producerCpu) {
    disruptor::BusySpinWaitStrategy waitStrategy;
    auto ringBuffer = disruptor::RingBuffer<ValueEvent>::createSingleProducer(
        [] { return ValueEvent{}; }, bufferSize, waitStrategy);

    disruptor::Sequence consumerSequence;
    ringBuffer.addGatingSequences({&consumerSequence});
    auto barrier = ringBuffer.newBarrier();

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<long long> sum{0};

    std::thread consumer([&] {
#ifdef __linux__
        if (!setAffinityStrict(consumerCpu)) {
            std::exit(3);
        }
#endif
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        long long localSum = 0;
        long next = 0;
        while (next < iterations) {
            long available = barrier.waitFor(next);
            if (available < next) {
                continue;
            }

            long hi = available;
            long last = iterations - 1;
            if (hi > last) {
                hi = last;
            }

            for (; next <= hi; ++next) {
                localSum += ringBuffer.get(next).value;
            }

            // Update gating sequence once per chunk.
            consumerSequence.set(hi);
        }
        sum.store(localSum, std::memory_order_relaxed);
    });

    std::thread producer([&] {
#ifdef __linux__
        if (!setAffinityStrict(producerCpu)) {
            std::exit(3);
        }
#endif
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (long i = 0; i < iterations; ++i) {
            long seq = ringBuffer.next();
            ringBuffer.get(seq).value = i;
            ringBuffer.publish(seq);
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
    RunResult r;
    r.seconds = seconds;
    r.opsPerSecond = static_cast<double>(iterations) / seconds;
    r.sum = sum.load(std::memory_order_relaxed);
    return r;
}

RunResult run_readerwriterqueue_spsc(long iterations, int capacity, int consumerCpu, int producerCpu) {
    moodycamel::ReaderWriterQueue<long> q(static_cast<size_t>(capacity));

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<long long> sum{0};

    std::thread consumer([&] {
#ifdef __linux__
        if (!setAffinityStrict(consumerCpu)) {
            std::exit(3);
        }
#endif
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        long long localSum = 0;
        long received = 0;
        long v = 0;
        while (received < iterations) {
            if (q.try_dequeue(v)) {
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
        if (!setAffinityStrict(producerCpu)) {
            std::exit(3);
        }
#endif
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (long i = 0; i < iterations; ++i) {
            while (!q.try_enqueue(i)) {
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
    RunResult r;
    r.seconds = seconds;
    r.opsPerSecond = static_cast<double>(iterations) / seconds;
    r.sum = sum.load(std::memory_order_relaxed);
    return r;
}

} // namespace

int main(int argc, char** argv) {
    long iterations = parseLong(argc > 1 ? argv[1] : nullptr, 10'000'000L);
    int capacity = static_cast<int>(parseLong(argc > 2 ? argv[2] : nullptr, 1 << 16));

    // Backward compatible:
    //   explicit: iterations capacity consumerCpu producerCpu
    // Optional mode:
    //   mode=0 (default): explicit CPU ids (strict check, no modulo)
    //   mode=1: auto pick 2 distinct physical cores in same NUMA node as baseCpu(arg3); producerCpu ignored
    int consumerCpu = static_cast<int>(parseLong(argc > 3 ? argv[3] : nullptr, 0));
    int producerCpu = static_cast<int>(parseLong(argc > 4 ? argv[4] : nullptr, 1));
    int mode = static_cast<int>(parseLong(argc > 5 ? argv[5] : nullptr, 0));

#ifdef __linux__
    auto cpus = enumerateCpus();
    if (cpus.empty()) {
        std::cerr << "Failed to enumerate CPUs from sysfs; cannot do strict pinning.\n";
        return 2;
    }

    if (mode == 1) {
        if (!cpuExistsOnline(cpus, consumerCpu)) {
            std::cerr << "baseCpu (arg3) not online/exists: " << consumerCpu << "\n";
            return 2;
        }
        auto picked = selectDistinctPhysicalCpusSameNode(cpus, consumerCpu, 2);
        if (static_cast<int>(picked.size()) < 2) {
            std::cerr << "Not enough CPUs to pin SPSC (need 2).\n";
            return 2;
        }
        consumerCpu = picked[0];
        producerCpu = picked[1];
    } else {
        if (!cpuExistsOnline(cpus, consumerCpu) || !cpuExistsOnline(cpus, producerCpu)) {
            std::cerr << "CPU id not online/exists: consumerCpu=" << consumerCpu << ", producerCpu=" << producerCpu << "\n";
            return 2;
        }
    }

    std::cout << "Benchmark: SPSC (each message consumed once)\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Queue/Ring capacity: " << capacity << "\n";
    std::cout << "Pinning: consumer->CPU" << consumerCpu << ", producer->CPU" << producerCpu << "\n";
    std::cout << "Pin mode: " << (mode == 1 ? "auto-numa+physical" : "explicit-strict") << "\n\n";
#else
    std::cout << "Benchmark: SPSC (each message consumed once)\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Queue/Ring capacity: " << capacity << "\n\n";
#endif

    // Warmup
    (void)run_disruptor_spsc(200'000L, capacity, consumerCpu, producerCpu);
    (void)run_readerwriterqueue_spsc(200'000L, capacity, consumerCpu, producerCpu);

    auto d = run_disruptor_spsc(iterations, capacity, consumerCpu, producerCpu);
    auto q = run_readerwriterqueue_spsc(iterations, capacity, consumerCpu, producerCpu);

    std::cout << "Disruptor-CPP:\n";
    std::cout << "  Time(s): " << d.seconds << "\n";
    std::cout << "  Throughput(msg/s): " << d.opsPerSecond << "\n";
    std::cout << "  Sum: " << d.sum << "\n\n";

    std::cout << "moodycamel::ReaderWriterQueue:\n";
    std::cout << "  Time(s): " << q.seconds << "\n";
    std::cout << "  Throughput(msg/s): " << q.opsPerSecond << "\n";
    std::cout << "  Sum: " << q.sum << "\n\n";

    std::cout << "Speedup (Disruptor / ReaderWriterQueue): " << (d.opsPerSecond / q.opsPerSecond) << "x\n";
    return 0;
}
