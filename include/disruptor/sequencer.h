#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "exceptions.h"
#include "sequence.h"
#include "util.h"
#include "wait_strategy.h"

namespace disruptor
{
inline bool isPowerOfTwo(int value)
{
    return value > 0 && (value & (value - 1)) == 0;
}

inline int log2i(int value)
{
    int r = 0;
    while ((1 << r) < value)
    {
        ++r;
    }
    return r;
}

class Sequencer
{
public:
    virtual ~Sequencer() = default;
    virtual int getBufferSize() const = 0;
    virtual Sequence& getCursor() = 0;
    virtual WaitStrategy& getWaitStrategy() = 0;

    virtual bool hasAvailableCapacity(int requiredCapacity) = 0;
    virtual long remainingCapacity() = 0;
    virtual long next() = 0;
    virtual long next(int n) = 0;
    virtual long tryNext() = 0;
    virtual long tryNext(int n) = 0;
    virtual void publish(long sequence) = 0;
    virtual void publish(long lo, long hi) = 0;
    virtual bool isAvailable(long sequence) = 0;
    virtual long getHighestPublishedSequence(long lowerBound, long availableSequence) = 0;

    virtual void addGatingSequences(const std::vector<Sequence*>& sequences) = 0;
    virtual bool removeGatingSequence(Sequence* sequence) = 0;
};

class AbstractSequencer : public Sequencer
{
public:
    AbstractSequencer(int bufferSize, WaitStrategy& waitStrategy)
        : bufferSize(bufferSize), waitStrategy(waitStrategy), cursor(Sequence::INITIAL_VALUE)
    {
        assert(isPowerOfTwo(bufferSize));
    }

    int getBufferSize() const override { return bufferSize; }
    Sequence& getCursor() override { return cursor; }
    WaitStrategy& getWaitStrategy() override { return waitStrategy; }

    void addGatingSequences(const std::vector<Sequence*>& sequences) override
    {
        gatingSequences.insert(gatingSequences.end(), sequences.begin(), sequences.end());
    }

    bool removeGatingSequence(Sequence* sequence) override
    {
        auto it = std::find(gatingSequences.begin(), gatingSequences.end(), sequence);
        if (it == gatingSequences.end())
        {
            return false;
        }
        gatingSequences.erase(it);
        return true;
    }

protected:
    int bufferSize;
    WaitStrategy& waitStrategy;
    Sequence cursor;
    std::vector<Sequence*> gatingSequences;
};

class SingleProducerSequencer final : public AbstractSequencer
{
public:
    SingleProducerSequencer(int bufferSize, WaitStrategy& waitStrategy)
        : AbstractSequencer(bufferSize, waitStrategy)
    {
    }

    bool hasAvailableCapacity(int requiredCapacity) override
    {
        return hasAvailableCapacity(requiredCapacity, false);
    }

    long remainingCapacity() override
    {
        long consumed = getMinimumSequence(gatingSequences, nextValue);
        long produced = nextValue;
        return bufferSize - (produced - consumed);
    }

    long next() override { return next(1); }

    long next(int n) override
    {
        if (n < 1 || n > bufferSize)
        {
            throw std::invalid_argument("n must be > 0 and < bufferSize");
        }

        long nextSeq = nextValue + n;
        long wrapPoint = nextSeq - bufferSize;
        long cachedGating = cachedValue;

        if (wrapPoint > cachedGating || cachedGating > nextValue)
        {
            cursor.setVolatile(nextValue);
            long minSequence;
            while (wrapPoint > (minSequence = getMinimumSequence(gatingSequences, nextValue)))
            {
                std::this_thread::yield();
            }
            cachedValue = minSequence;
        }

        nextValue = nextSeq;
        return nextSeq;
    }

    long tryNext() override { return tryNext(1); }

    long tryNext(int n) override
    {
        if (n < 1)
        {
            throw std::invalid_argument("n must be > 0");
        }

        if (!hasAvailableCapacity(n, true))
        {
            throw InsufficientCapacityException();
        }

        nextValue += n;
        return nextValue;
    }

    void publish(long sequence) override
    {
        cursor.set(sequence);
        waitStrategy.signalAllWhenBlocking();
    }

    void publish(long, long hi) override
    {
        publish(hi);
    }

    bool isAvailable(long sequence) override
    {
        long current = cursor.get();
        return sequence <= current && sequence > current - bufferSize;
    }

    long getHighestPublishedSequence(long, long availableSequence) override
    {
        return availableSequence;
    }

private:
    bool hasAvailableCapacity(int requiredCapacity, bool doStore)
    {
        long wrapPoint = (nextValue + requiredCapacity) - bufferSize;
        long cachedGating = cachedValue;

        if (wrapPoint > cachedGating || cachedGating > nextValue)
        {
            if (doStore)
            {
                cursor.setVolatile(nextValue);
            }

            long minSequence = getMinimumSequence(gatingSequences, nextValue);
            cachedValue = minSequence;
            if (wrapPoint > minSequence)
            {
                return false;
            }
        }

        return true;
    }

    long nextValue = Sequence::INITIAL_VALUE;
    long cachedValue = Sequence::INITIAL_VALUE;
};

class MultiProducerSequencer final : public AbstractSequencer
{
public:
    MultiProducerSequencer(int bufferSize, WaitStrategy& waitStrategy)
        : AbstractSequencer(bufferSize, waitStrategy),
          availableBuffer(bufferSize),
          indexMask(bufferSize - 1),
          indexShift(log2i(bufferSize))
    {
        for (auto& slot : availableBuffer)
        {
            slot.store(-1, std::memory_order_relaxed);
        }
    }

    bool hasAvailableCapacity(int requiredCapacity) override
    {
        return hasAvailableCapacity(requiredCapacity, cursor.get());
    }

    long remainingCapacity() override
    {
        long consumed = getMinimumSequence(gatingSequences, cursor.get());
        long produced = cursor.get();
        return bufferSize - (produced - consumed);
    }

    long next() override { return next(1); }

    long next(int n) override
    {
        if (n < 1 || n > bufferSize)
        {
            throw std::invalid_argument("n must be > 0 and < bufferSize");
        }

        long current = cursor.getAndAdd(n);
        long nextSequence = current + n;
        long wrapPoint = nextSequence - bufferSize;
        long cachedGating = gatingSequenceCache.get();

        if (wrapPoint > cachedGating || cachedGating > current)
        {
            long gatingSequence;
            while (wrapPoint > (gatingSequence = getMinimumSequence(gatingSequences, current)))
            {
                std::this_thread::yield();
            }
            gatingSequenceCache.set(gatingSequence);
        }

        return nextSequence;
    }

    long tryNext() override { return tryNext(1); }

    long tryNext(int n) override
    {
        if (n < 1)
        {
            throw std::invalid_argument("n must be > 0");
        }

        long current;
        long nextSequence;
        do
        {
            current = cursor.get();
            nextSequence = current + n;
            if (!hasAvailableCapacity(n, current))
            {
                throw InsufficientCapacityException();
            }
        }
        while (!cursor.compareAndSet(current, nextSequence));

        return nextSequence;
    }

    void publish(long sequence) override
    {
        setAvailable(sequence);
        waitStrategy.signalAllWhenBlocking();
    }

    void publish(long lo, long hi) override
    {
        for (long s = lo; s <= hi; ++s)
        {
            setAvailable(s);
        }
        waitStrategy.signalAllWhenBlocking();
    }

    bool isAvailable(long sequence) override
    {
        int index = calculateIndex(sequence);
        int flag = calculateAvailabilityFlag(sequence);
        return availableBuffer[index].load(std::memory_order_acquire) == flag;
    }

    long getHighestPublishedSequence(long lowerBound, long availableSequence) override
    {
        for (long sequence = lowerBound; sequence <= availableSequence; ++sequence)
        {
            if (!isAvailable(sequence))
            {
                return sequence - 1;
            }
        }
        return availableSequence;
    }

private:
    bool hasAvailableCapacity(int requiredCapacity, long cursorValue)
    {
        long wrapPoint = (cursorValue + requiredCapacity) - bufferSize;
        long cachedGating = gatingSequenceCache.get();

        if (wrapPoint > cachedGating || cachedGating > cursorValue)
        {
            long minSequence = getMinimumSequence(gatingSequences, cursorValue);
            gatingSequenceCache.set(minSequence);
            if (wrapPoint > minSequence)
            {
                return false;
            }
        }

        return true;
    }

    void setAvailable(long sequence)
    {
        int index = calculateIndex(sequence);
        int flag = calculateAvailabilityFlag(sequence);
        availableBuffer[index].store(flag, std::memory_order_release);
    }

    int calculateAvailabilityFlag(long sequence) const
    {
        return static_cast<int>(sequence >> indexShift);
    }

    int calculateIndex(long sequence) const
    {
        return static_cast<int>(sequence) & indexMask;
    }

    Sequence gatingSequenceCache{Sequence::INITIAL_VALUE};
    std::vector<std::atomic<int>> availableBuffer;
    int indexMask;
    int indexShift;
};
} // namespace disruptor
