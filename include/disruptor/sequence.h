#pragma once

#include <atomic>
#include <cstddef>

#include "cache_line_storage.h"

namespace disruptor
{

struct SequenceValue
{
    std::atomic<long> value{-1L};
};

/**
 * Sequence with cache-line padding using generic CacheLineStorage template.
 * Prevents false sharing in multi-core scenarios.
 *
 * Memory ordering:
 * - get(): atomic load acquire
 * - set(): atomic store release
 */
class Sequence
{
public:
    static constexpr long INITIAL_VALUE = -1L;

    explicit Sequence(long initialValue = INITIAL_VALUE) noexcept
    {
        storage_.data.value.store(initialValue, std::memory_order_relaxed);
    }

    // Relaxed read - use when ordering is not critical (hot path)
    long getRelaxed() const noexcept
    {
        return storage_.data.value.load(std::memory_order_relaxed);
    }

    // Acquire read - use for synchronization
    long get() const noexcept
    {
        return storage_.data.value.load(std::memory_order_acquire);
    }

    // Release write - use for synchronization
    void set(long v) noexcept
    {
        storage_.data.value.store(v, std::memory_order_release);
    }

    // Relaxed write - use when ordering is not critical (producer local var)
    void setRelaxed(long v) noexcept
    {
        storage_.data.value.store(v, std::memory_order_relaxed);
    }

    // Strong ordering write (seq_cst) for rare cases
    void setVolatile(long v) noexcept
    {
        storage_.data.value.store(v, std::memory_order_seq_cst);
    }

    bool compareAndSet(long expected, long desired) noexcept
    {
        return storage_.data.value.compare_exchange_strong(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    long incrementAndGet() noexcept
    {
        return addAndGet(1);
    }

    long addAndGet(long increment) noexcept
    {
        return storage_.data.value.fetch_add(increment, std::memory_order_acq_rel) + increment;
    }

    long getAndAdd(long increment) noexcept
    {
        return storage_.data.value.fetch_add(increment, std::memory_order_acq_rel);
    }

private:
    CacheLineStorage<SequenceValue, CACHE_LINE_SIZE, CACHE_LINE_SIZE * 2> storage_;
};

// Compile-time verification
static_assert(sizeof(Sequence) == 128, "Sequence must be 128 bytes for proper cache alignment");
static_assert(alignof(Sequence) == 128, "Sequence must be 128-byte aligned");

} // namespace disruptor
