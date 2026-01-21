#pragma once

#include <atomic>
#include <cstddef>

#include "cache_line_storage.h"

namespace disruptor
{

/**
 * Internal value holder with union for atomic/non-atomic access.
 * This allows Java-style fence + plain write for maximum performance.
 */
struct SequenceValue
{
    union {
        long value;
        std::atomic<long> atomic;
    };
    
    SequenceValue() noexcept : value(-1L) {}  // Default to INITIAL_VALUE
};

/**
 * Sequence with cache-line padding using generic CacheLineStorage template.
 * Prevents false sharing in multi-core scenarios.
 * 
 * Uses Java-style memory ordering for maximum performance:
 * - get(): plain read + acquire fence
 * - set(): release fence + plain write
 * 
 * Memory layout (128 bytes total, 128-byte aligned):
 *   [left_padding][SequenceValue][right_padding]
 */
class Sequence
{
public:
    static constexpr long INITIAL_VALUE = -1L;

    explicit Sequence(long initialValue = INITIAL_VALUE) noexcept
    {
        storage_.data.value = initialValue;
        std::atomic_thread_fence(std::memory_order_release);
    }

    // Relaxed read - use when ordering is not critical (hot path)
    long getRelaxed() const noexcept
    {
        return storage_.data.value;
    }

    // Acquire read - use for synchronization (Java style: plain read + acquire fence)
    long get() const noexcept
    {
        long v = storage_.data.value;
        std::atomic_thread_fence(std::memory_order_acquire);
        return v;
    }

    // Release write - use for synchronization (Java style: release fence + plain write)
    // This is 2x faster than atomic store with release ordering
    void set(long v) noexcept
    {
        std::atomic_thread_fence(std::memory_order_release);
        storage_.data.value = v;
    }

    // Relaxed write - use when ordering is not critical (producer local var)
    void setRelaxed(long v) noexcept
    {
        storage_.data.value = v;
    }

    // Volatile write (Java style: release fence + write + full fence)
    void setVolatile(long v) noexcept
    {
        std::atomic_thread_fence(std::memory_order_release);
        storage_.data.value = v;
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    // For atomic operations, use internal atomic
    bool compareAndSet(long expected, long desired) noexcept
    {
        return storage_.data.atomic.compare_exchange_strong(expected, desired, std::memory_order_acq_rel);
    }

    long incrementAndGet() noexcept
    {
        return addAndGet(1);
    }

    long addAndGet(long increment) noexcept
    {
        return storage_.data.atomic.fetch_add(increment, std::memory_order_acq_rel) + increment;
    }

    long getAndAdd(long increment) noexcept
    {
        return storage_.data.atomic.fetch_add(increment, std::memory_order_acq_rel);
    }

private:
    // Use generic cache line storage template with 2x cache line for extra safety
    CacheLineStorage<SequenceValue, CACHE_LINE_SIZE, CACHE_LINE_SIZE * 2> storage_;
};

// Compile-time verification
static_assert(sizeof(Sequence) == 128, "Sequence must be 128 bytes for proper cache alignment");
static_assert(alignof(Sequence) == 128, "Sequence must be 128-byte aligned");

} // namespace disruptor
