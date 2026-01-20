#pragma once

#include <atomic>
#include <cstddef>

namespace disruptor
{
class Sequence
{
public:
    static constexpr long INITIAL_VALUE = -1L;

    explicit Sequence(long initialValue = INITIAL_VALUE) noexcept
        : value(initialValue)
    {
    }

    long get() const noexcept
    {
        return value.load(std::memory_order_acquire);
    }

    void set(long v) noexcept
    {
        value.store(v, std::memory_order_release);
    }

    void setVolatile(long v) noexcept
    {
        value.store(v, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    bool compareAndSet(long expected, long desired) noexcept
    {
        return value.compare_exchange_strong(expected, desired, std::memory_order_acq_rel);
    }

    long incrementAndGet() noexcept
    {
        return addAndGet(1);
    }

    long addAndGet(long increment) noexcept
    {
        return value.fetch_add(increment, std::memory_order_acq_rel) + increment;
    }

    long getAndAdd(long increment) noexcept
    {
        return value.fetch_add(increment, std::memory_order_acq_rel);
    }

private:
    alignas(64) std::atomic<long> value;
    char padding[64]{};
};
} // namespace disruptor
