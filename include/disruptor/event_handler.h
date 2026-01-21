#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace disruptor
{
/**
 * Standard event handler - processes events one at a time.
 */
template <typename T>
class EventHandler
{
public:
    virtual ~EventHandler() = default;

    /**
     * Called when a new event is available.
     * @param event The event to process
     * @param sequence The sequence number of this event
     * @param endOfBatch True if this is the last event in the current batch
     */
    virtual void onEvent(T& event, long sequence, bool endOfBatch) = 0;

    /**
     * Called when the processor starts.
     */
    virtual void onStart() {}

    /**
     * Called when the processor shuts down.
     */
    virtual void onShutdown() {}
};

/**
 * CRTP (Curiously Recurring Template Pattern) event handler base class.
 * Eliminates virtual function call overhead through compile-time polymorphism.
 * 
 * Usage:
 *   class MyHandler : public StaticEventHandler<MyHandler, EventType> {
 *   public:
 *       void handleEvent(EventType& event, long sequence, bool endOfBatch) {
 *           // process event
 *       }
 *   };
 */
template <typename Derived, typename T>
class StaticEventHandler
{
public:
    // Non-virtual, inlined call to derived class
    void onEvent(T& event, long sequence, bool endOfBatch)
    {
        static_cast<Derived*>(this)->handleEvent(event, sequence, endOfBatch);
    }

    void onStart()
    {
        if constexpr (requires(Derived& d) { d.handleStart(); })
        {
            static_cast<Derived*>(this)->handleStart();
        }
    }

    void onShutdown()
    {
        if constexpr (requires(Derived& d) { d.handleShutdown(); })
        {
            static_cast<Derived*>(this)->handleShutdown();
        }
    }
};

/**
 * High-performance event handler base class.
 * Eliminates atomic operations in hot path by using local variables.
 * Only updates shared state at batch end or when completion is reached.
 * 
 * Usage:
 *   class MyHandler : public FastEventHandler<EventType> {
 *   public:
 *       void processEvent(EventType& event, long sequence) override {
 *           localSum_ += event.value;  // Use protected local variables
 *       }
 *   };
 */
template <typename T>
class FastEventHandler : public EventHandler<T>
{
public:
    void reset(long expectedCount)
    {
        expectedCount_ = expectedCount;
        localCount_ = 0;
        localSum_ = 0;
        done_.store(false, std::memory_order_relaxed);
    }

    void onEvent(T& event, long sequence, bool endOfBatch) final
    {
        processEvent(event, sequence);
        ++localCount_;

        // Only check completion at batch end to reduce overhead
        if (endOfBatch && localCount_ >= expectedCount_)
        {
            done_.store(true, std::memory_order_release);
            std::lock_guard<std::mutex> lock(mutex_);
            cv_.notify_all();
        }
    }

    void waitForExpected()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return done_.load(std::memory_order_acquire); });
    }

    long long getSum() const { return localSum_; }
    long getCount() const { return localCount_; }

protected:
    // Override this to process events - use localSum_ for accumulation
    virtual void processEvent(T& event, long sequence) = 0;

    // Local variables - no atomic overhead
    long localCount_ = 0;
    long long localSum_ = 0;
    long expectedCount_ = 0;

private:
    std::atomic<bool> done_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
};

/**
 * High-performance event handler with ID support.
 * For use cases where multiple handlers need identification.
 */
template <typename T>
class FastEventHandlerWithId : public EventHandler<T>
{
public:
    explicit FastEventHandlerWithId(int id) : handlerId_(id) {}

    void reset(long expectedCount)
    {
        expectedCount_ = expectedCount;
        localCount_ = 0;
        localSum_ = 0;
        done_.store(false, std::memory_order_relaxed);
    }

    void onEvent(T& event, long sequence, bool endOfBatch) final
    {
        processEvent(event, sequence);
        ++localCount_;

        if (endOfBatch && localCount_ >= expectedCount_)
        {
            done_.store(true, std::memory_order_release);
            std::lock_guard<std::mutex> lock(mutex_);
            cv_.notify_all();
        }
    }

    void waitForExpected()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return done_.load(std::memory_order_acquire); });
    }

    long long getSum() const { return localSum_; }
    long getCount() const { return localCount_; }
    int getId() const { return handlerId_; }

protected:
    virtual void processEvent(T& event, long sequence) = 0;

    long localCount_ = 0;
    long long localSum_ = 0;
    long expectedCount_ = 0;
    int handlerId_;

private:
    std::atomic<bool> done_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
};

/**
 * High-performance batch event handler.
 * Processes multiple events at once for maximum throughput.
 */
template <typename T>
class BatchEventHandler
{
public:
    virtual ~BatchEventHandler() = default;

    /**
     * Called with a batch of events.
     * @param events Pointer to first event in the array
     * @param indices Array of indices into events array
     * @param count Number of events in this batch
     * @param startSequence Sequence number of first event
     */
    virtual void onBatch(T* events, const size_t* indices, int count, long startSequence) = 0;

    /**
     * Called when the processor starts.
     */
    virtual void onStart() {}

    /**
     * Called when the processor shuts down.
     */
    virtual void onShutdown() {}
};

} // namespace disruptor
