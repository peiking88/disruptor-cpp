#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "producer_sequencer.h"
#include "consumer_barrier.h"

namespace disruptor
{

/**
 * High-performance batch publisher for maximum throughput.
 * Holds claimed sequences and publishes them in batch.
 */
template <typename T>
class BatchPublisher;

template <typename T>
class RingBuffer
{
public:
    using Factory = std::function<T()>;

    static RingBuffer createSingleProducer(Factory factory, int bufferSize, WaitStrategy& waitStrategy)
    {
        return RingBuffer(std::move(factory), std::make_unique<SingleProducerSequencer>(bufferSize, waitStrategy));
    }

    static RingBuffer createMultiProducer(Factory factory, int bufferSize, WaitStrategy& waitStrategy)
    {
        return RingBuffer(std::move(factory), std::make_unique<MultiProducerSequencer>(bufferSize, waitStrategy));
    }

    long next() { return sequencer->next(); }
    long next(int n) { return sequencer->next(n); }
    long tryNext() { return sequencer->tryNext(); }
    long tryNext(int n) { return sequencer->tryNext(n); }

    void publish(long sequence) { sequencer->publish(sequence); }
    void publish(long lo, long hi) { sequencer->publish(lo, hi); }

    T& get(long sequence)
    {
        return entries[static_cast<size_t>(sequence) & indexMask_];
    }

    /**
     * Get pointer to event at sequence. 
     * Use for batch processing to avoid reference overhead.
     */
    T* getPointer(long sequence)
    {
        return &entries[static_cast<size_t>(sequence) & indexMask_];
    }

    /**
     * Get raw entries array for direct batch access.
     * Use with getIndex() for maximum performance.
     */
    T* getEntries() { return entries.data(); }
    const T* getEntries() const { return entries.data(); }

    /**
     * Calculate index for a sequence. Use with getEntries() for batch processing.
     */
    size_t getIndex(long sequence) const
    {
        return static_cast<size_t>(sequence) & indexMask_;
    }

    /**
     * Get index mask for direct calculation.
     */
    size_t getIndexMask() const { return indexMask_; }

    long getCursor() const { return sequencer->getCursor().get(); }

    SequenceBarrier newBarrier(const std::vector<Sequence*>& dependents = {})
    {
        return SequenceBarrier(sequencer->getWaitStrategy(), sequencer->getCursor(), dependents,
                               sequencer.get());
    }

    void addGatingSequences(const std::vector<Sequence*>& sequences)
    {
        sequencer->addGatingSequences(sequences);
    }

    bool removeGatingSequence(Sequence* sequence)
    {
        return sequencer->removeGatingSequence(sequence);
    }

    int getBufferSize() const { return bufferSize; }

    /**
     * Create a batch publisher for high-throughput batch publishing.
     */
    BatchPublisher<T> createBatchPublisher(int batchSize = 100);

private:
    RingBuffer(Factory factory, std::unique_ptr<Sequencer> sequencer)
        : bufferSize(sequencer->getBufferSize()), 
          indexMask_(static_cast<size_t>(bufferSize - 1)),
          entries(static_cast<size_t>(bufferSize)), 
          sequencer(std::move(sequencer))
    {
        for (int i = 0; i < bufferSize; ++i)
        {
            entries[static_cast<size_t>(i)] = factory();
        }
    }

    int bufferSize;
    size_t indexMask_;
    std::vector<T> entries;
    std::unique_ptr<Sequencer> sequencer;
};

/**
 * High-performance batch publisher.
 * 
 * Two modes of operation:
 * 
 * Mode 1: Fixed batch size (simple API)
 *   auto publisher = ringBuffer.createBatchPublisher(100);
 *   for (...) {
 *       auto& event = publisher.claim();
 *       event.value = ...;
 *       if (publisher.isFull()) publisher.publishBatch();
 *   }
 *   publisher.publishBatch();
 * 
 * Mode 2: Dynamic batch size (Java-style, more flexible)
 *   publisher.beginBatch(n);  // claim n slots
 *   for (int i = 0; i < n; ++i) {
 *       publisher.getEvent(i).value = ...;
 *   }
 *   publisher.endBatch();  // publish all
 */
template <typename T>
class BatchPublisher
{
public:
    BatchPublisher(RingBuffer<T>& ringBuffer, int defaultBatchSize = 100)
        : ringBuffer_(ringBuffer), defaultBatchSize_(defaultBatchSize)
    {
    }

    // ============ Mode 1: Fixed batch size API ============

    /**
     * Claim next event slot. Call publishBatch() when batch is full.
     */
    T& claim()
    {
        if (currentBatchSize_ == 0)
        {
            // Claim new batch with default size
            highSequence_ = ringBuffer_.next(defaultBatchSize_);
            lowSequence_ = highSequence_ - defaultBatchSize_ + 1;
            nextSequence_ = lowSequence_;
            batchCapacity_ = defaultBatchSize_;
        }
        
        T& event = ringBuffer_.get(nextSequence_);
        ++nextSequence_;
        ++currentBatchSize_;
        return event;
    }

    /**
     * Check if batch is full and should be published.
     */
    bool isFull() const
    {
        return currentBatchSize_ >= batchCapacity_;
    }

    /**
     * Publish all claimed events in batch.
     */
    void publishBatch()
    {
        if (currentBatchSize_ > 0)
        {
            ringBuffer_.publish(lowSequence_, lowSequence_ + currentBatchSize_ - 1);
            currentBatchSize_ = 0;
        }
    }

    // ============ Mode 2: Dynamic batch size API (Java-style) ============

    /**
     * Begin a batch of specified size. More flexible than fixed batch.
     * @param size Number of events to claim
     */
    void beginBatch(int size)
    {
        highSequence_ = ringBuffer_.next(size);
        lowSequence_ = highSequence_ - size + 1;
        batchCapacity_ = size;
        currentBatchSize_ = size;  // All slots are claimed
    }

    /**
     * Try to begin a batch. Returns false if not enough capacity.
     * @param size Number of events to claim
     */
    bool tryBeginBatch(int size)
    {
        try
        {
            highSequence_ = ringBuffer_.tryNext(size);
            lowSequence_ = highSequence_ - size + 1;
            batchCapacity_ = size;
            currentBatchSize_ = size;
            return true;
        }
        catch (const InsufficientCapacityException&)
        {
            return false;
        }
    }

    /**
     * Get event at index within current batch.
     * @param index Index within batch (0 to size-1)
     */
    T& getEvent(int index)
    {
        return ringBuffer_.get(lowSequence_ + index);
    }

    /**
     * Get sequence number at index within current batch.
     */
    long getSequence(int index) const
    {
        return lowSequence_ + index;
    }

    /**
     * End batch and publish all events.
     */
    void endBatch()
    {
        ringBuffer_.publish(lowSequence_, highSequence_);
        currentBatchSize_ = 0;
    }

    /**
     * End batch and publish only first n events (partial publish).
     */
    void endBatch(int count)
    {
        if (count > 0 && count <= batchCapacity_)
        {
            ringBuffer_.publish(lowSequence_, lowSequence_ + count - 1);
        }
        currentBatchSize_ = 0;
    }

    // ============ Accessors ============

    int getCurrentBatchSize() const { return currentBatchSize_; }
    int getBatchCapacity() const { return batchCapacity_; }
    long getLowSequence() const { return lowSequence_; }
    long getHighSequence() const { return highSequence_; }

private:
    RingBuffer<T>& ringBuffer_;
    int defaultBatchSize_;
    int batchCapacity_ = 0;
    int currentBatchSize_ = 0;
    long highSequence_ = 0;
    long lowSequence_ = 0;
    long nextSequence_ = 0;
};

template <typename T>
BatchPublisher<T> RingBuffer<T>::createBatchPublisher(int batchSize)
{
    return BatchPublisher<T>(*this, batchSize);
}

} // namespace disruptor
