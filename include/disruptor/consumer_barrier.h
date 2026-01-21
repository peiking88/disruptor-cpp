#pragma once

#include <atomic>
#include <vector>

#include "exceptions.h"
#include "sequence.h"
#include "wait_strategy.h"

namespace disruptor
{
class SequenceBarrier
{
public:
    SequenceBarrier(WaitStrategy& waitStrategy, Sequence& cursor, std::vector<Sequence*> dependents)
        : waitStrategy(&waitStrategy), cursor(&cursor), dependents(std::move(dependents))
    {
    }

    // 支持移动语义
    SequenceBarrier(SequenceBarrier&& other) noexcept
        : waitStrategy(other.waitStrategy),
          cursor(other.cursor),
          dependents(std::move(other.dependents)),
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
            alerted.store(other.alerted.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }

    // 禁用复制
    SequenceBarrier(const SequenceBarrier&) = delete;
    SequenceBarrier& operator=(const SequenceBarrier&) = delete;

    long waitFor(long sequence)
    {
        return waitStrategy->waitFor(sequence, *cursor, dependents, alerted);
    }

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
    std::atomic<bool> alerted{false};
};
} // namespace disruptor
