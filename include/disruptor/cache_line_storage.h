#pragma once

#include <cstddef>
#include <type_traits>

namespace disruptor
{

// Default cache line size (most modern CPUs use 64 bytes)
inline constexpr std::size_t CACHE_LINE_SIZE = 64;

/**
 * Generic cache-line storage template with double-sided padding.
 * Prevents false sharing by ensuring data is isolated in its own cache line(s).
 * 
 * Features:
 * - Double-sided padding (both before and after data)
 * - Compile-time padding calculation
 * - Type-safe and reusable
 * - Supports any data type (including non-copyable types)
 * 
 * Memory layout:
 *   [left_padding][data][right_padding]
 *   
 * @tparam T The type to store
 * @tparam CacheLineSize Cache line size in bytes (default: 64)
 * @tparam Alignment Overall alignment (default: 2 * CacheLineSize for extra safety)
 */
template<typename T, 
         std::size_t CacheLineSize = CACHE_LINE_SIZE,
         std::size_t Alignment = CacheLineSize * 2>
struct alignas(Alignment) CacheLineStorage
{
    static_assert(CacheLineSize >= sizeof(T), 
        "CacheLineSize must be >= sizeof(T). Use larger CacheLineSize for big types.");
    
    // Calculate padding to center the data within the aligned storage
    static constexpr std::size_t DATA_SIZE = sizeof(T);
    static constexpr std::size_t LEFT_PADDING = (CacheLineSize - DATA_SIZE) / 2;
    static constexpr std::size_t RIGHT_PADDING = CacheLineSize - DATA_SIZE - LEFT_PADDING;
    
    // Left padding
    char leftPadding[LEFT_PADDING > 0 ? LEFT_PADDING : 1]{};
    
    // The actual data
    T data;
    
    // Right padding
    char rightPadding[RIGHT_PADDING > 0 ? RIGHT_PADDING : 1]{};
    
    // Default constructor
    CacheLineStorage() = default;
    
    // Copy constructor (only if T is copyable)
    CacheLineStorage(const CacheLineStorage&) = default;
    
    // Move constructor (only if T is movable)
    CacheLineStorage(CacheLineStorage&&) = default;
    
    // Assignment operators
    CacheLineStorage& operator=(const CacheLineStorage&) = default;
    CacheLineStorage& operator=(CacheLineStorage&&) = default;
    
    // Access operators
    T& operator*() noexcept { return data; }
    const T& operator*() const noexcept { return data; }
    
    T* operator->() noexcept { return &data; }
    const T* operator->() const noexcept { return &data; }
    
    // Implicit conversion
    operator T&() noexcept { return data; }
    operator const T&() const noexcept { return data; }
};

/**
 * Simplified alias for common use case: single cache line storage.
 */
template<typename T>
using CachePadded = CacheLineStorage<T, CACHE_LINE_SIZE, CACHE_LINE_SIZE>;

/**
 * Double cache line storage for extra safety (prevents prefetch interference).
 */
template<typename T>
using CachePadded2x = CacheLineStorage<T, CACHE_LINE_SIZE, CACHE_LINE_SIZE * 2>;

} // namespace disruptor
