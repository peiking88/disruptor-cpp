#pragma once

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
