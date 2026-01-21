#pragma once

#include <atomic>
#include <vector>

#include "exceptions.h"
#include "sequence.h"
#include "wait_strategy.h"

namespace disruptor
{

// Forward declaration
class Sequencer;

class SequenceBarrier
{
public:
    SequenceBarrier(WaitStrategy& waitStrategy, Sequence& cursor, std::vector<Sequence*> dependents,
                    Sequencer* sequencer = nullptr)
        : waitStrategy(&waitStrategy), cursor(&cursor), dependents(std::move(dependents)),
          sequencer(sequencer)
    {
    }

    // 支持移动语义
    SequenceBarrier(SequenceBarrier&& other) noexcept
        : waitStrategy(other.waitStrategy),
          cursor(other.cursor),
          dependents(std::move(other.dependents)),
          sequencer(other.sequencer),
          alerted(other.alerted.load(std::memory_order_relaxed))
    {
    }

    SequenceBarrier& operator=(SequenceBarrier&& other) noexcept
    {
        if (this != &other)
        {
            waitStrategy = other.waitStrategy;
            cursor = other.cursor;
            dependents = std::move(other.dependents);
            sequencer = other.sequencer;
            alerted.store(other.alerted.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }

    // 禁用复制
    SequenceBarrier(const SequenceBarrier&) = delete;
    SequenceBarrier& operator=(const SequenceBarrier&) = delete;

    /**
     * Wait for the given sequence to be available.
     * For MultiProducer, this ensures all sequences up to the returned value are published.
     */
    long waitFor(long sequence);

    void alert()
    {
        alerted.store(true, std::memory_order_release);
        waitStrategy->signalAllWhenBlocking();
    }

    void clearAlert()
    {
        alerted.store(false, std::memory_order_release);
    }

    bool isAlerted() const
    {
        return alerted.load(std::memory_order_acquire);
    }

    long getCursor() const
    {
        return cursor->get();
    }

private:
    WaitStrategy* waitStrategy;
    Sequence* cursor;
    std::vector<Sequence*> dependents;
    Sequencer* sequencer;  // For getHighestPublishedSequence in MultiProducer
    std::atomic<bool> alerted{false};
};

} // namespace disruptor

// Include after class definition to avoid circular dependency
#include "producer_sequencer.h"

namespace disruptor
{

inline long SequenceBarrier::waitFor(long sequence)
{
    long availableSequence = waitStrategy->waitFor(sequence, *cursor, dependents, alerted);
    
    // For MultiProducer, we need to check if all sequences are actually published
    // getHighestPublishedSequence returns the highest sequence that is fully published
    if (sequencer != nullptr)
    {
        availableSequence = sequencer->getHighestPublishedSequence(sequence, availableSequence);
    }
    
    return availableSequence;
}

} // namespace disruptor
