#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "exceptions.h"
#include "sequence.h"
#include "util.h"

// Platform-specific CPU pause instruction
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #define DISRUPTOR_CPU_PAUSE() __builtin_ia32_pause()
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define DISRUPTOR_CPU_PAUSE() asm volatile("yield" ::: "memory")
#else
    #define DISRUPTOR_CPU_PAUSE() ((void)0)
#endif

namespace disruptor
{
class WaitStrategy
{
public:
    virtual ~WaitStrategy() = default;
    virtual long waitFor(long sequence, Sequence& cursor, const std::vector<Sequence*>& dependents,
        std::atomic<bool>& alerted) = 0;
    virtual void signalAllWhenBlocking() = 0;
};

/**
 * Busy spin wait strategy with batched alert checking.
 * Checks alert every 256 iterations and uses CPU pause instruction.
 */
class BusySpinWaitStrategy final : public WaitStrategy
{
public:
    long waitFor(long sequence, Sequence& cursor, const std::vector<Sequence*>& dependents,
        std::atomic<bool>& alerted) override
    {
        int counter = 0;
        while (true)
        {
            // Check alert every 256 iterations to reduce overhead
            if ((++counter & 0xFF) == 0)
            {
                if (alerted.load(std::memory_order_relaxed))
                {
                    throw AlertException();
                }
            }
            
            long available = dependents.empty() 
                ? cursor.get() 
                : getMinimumSequence(dependents, cursor.get());
            
            if (available >= sequence)
            {
                return available;
            }
            
            DISRUPTOR_CPU_PAUSE();
        }
    }

    void signalAllWhenBlocking() override {}
};

/**
 * Yielding wait strategy with spin-before-yield optimization.
 * Spins for SPIN_TRIES iterations before yielding to reduce context switches.
 */
class YieldingWaitStrategy final : public WaitStrategy
{
    static constexpr int SPIN_TRIES = 100;

public:
    long waitFor(long sequence, Sequence& cursor, const std::vector<Sequence*>& dependents,
        std::atomic<bool>& alerted) override
    {
        int counter = SPIN_TRIES;
        
        while (true)
        {
            long available = dependents.empty() 
                ? cursor.get() 
                : getMinimumSequence(dependents, cursor.get());
            
            if (available >= sequence)
            {
                return available;
            }
            
            if (counter == 0)
            {
                // Only check alert when about to yield (reduces overhead)
                if (alerted.load(std::memory_order_relaxed))
                {
                    throw AlertException();
                }
                std::this_thread::yield();
                counter = SPIN_TRIES;
            }
            else
            {
                --counter;
                DISRUPTOR_CPU_PAUSE();
            }
        }
    }

    void signalAllWhenBlocking() override {}
};

/**
 * Sleeping wait strategy with progressive backoff.
 * Spins -> Yields -> Sleeps progressively.
 */
class SleepingWaitStrategy final : public WaitStrategy
{
    static constexpr int SPIN_TRIES = 200;
    static constexpr int YIELD_TRIES = 100;

public:
    long waitFor(long sequence, Sequence& cursor, const std::vector<Sequence*>& dependents,
        std::atomic<bool>& alerted) override
    {
        int counter = SPIN_TRIES + YIELD_TRIES;
        
        while (true)
        {
            if (alerted.load(std::memory_order_relaxed))
            {
                throw AlertException();
            }
            
            long available = dependents.empty() 
                ? cursor.get() 
                : getMinimumSequence(dependents, cursor.get());
            
            if (available >= sequence)
            {
                return available;
            }
            
            if (counter > YIELD_TRIES)
            {
                // Spin phase
                --counter;
                DISRUPTOR_CPU_PAUSE();
            }
            else if (counter > 0)
            {
                // Yield phase
                --counter;
                std::this_thread::yield();
            }
            else
            {
                // Sleep phase - use short sleep for responsiveness
                std::this_thread::sleep_for(std::chrono::nanoseconds(100));
            }
        }
    }

    void signalAllWhenBlocking() override {}
};

/**
 * Blocking wait strategy using condition variable.
 * Best for low-latency, low-throughput scenarios.
 */
class BlockingWaitStrategy final : public WaitStrategy
{
public:
    long waitFor(long sequence, Sequence& cursor, const std::vector<Sequence*>& dependents,
        std::atomic<bool>& alerted) override
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (true)
        {
            if (alerted.load(std::memory_order_acquire))
            {
                throw AlertException();
            }

            long available = cursor.get();
            available = getMinimumSequence(dependents, available);
            if (available >= sequence)
            {
                return available;
            }
            cond.wait_for(lock, std::chrono::microseconds(50));
        }
    }

    void signalAllWhenBlocking() override
    {
        cond.notify_all();
    }

private:
    std::mutex mutex;
    std::condition_variable cond;
};
} // namespace disruptor
