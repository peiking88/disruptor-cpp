#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "exceptions.h"
#include "sequence.h"
#include "util.h"

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

class BusySpinWaitStrategy final : public WaitStrategy
{
public:
    long waitFor(long sequence, Sequence& cursor, const std::vector<Sequence*>& dependents,
        std::atomic<bool>& alerted) override
    {
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
        }
    }

    void signalAllWhenBlocking() override {}
};

class YieldingWaitStrategy final : public WaitStrategy
{
public:
    long waitFor(long sequence, Sequence& cursor, const std::vector<Sequence*>& dependents,
        std::atomic<bool>& alerted) override
    {
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
            std::this_thread::yield();
        }
    }

    void signalAllWhenBlocking() override {}
};

class SleepingWaitStrategy final : public WaitStrategy
{
public:
    long waitFor(long sequence, Sequence& cursor, const std::vector<Sequence*>& dependents,
        std::atomic<bool>& alerted) override
    {
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
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }

    void signalAllWhenBlocking() override {}
};

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
