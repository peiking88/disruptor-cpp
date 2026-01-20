#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "sequencer.h"
#include "sequence_barrier.h"

namespace disruptor
{
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
        return entries[static_cast<size_t>(sequence) & (bufferSize - 1)];
    }

    long getCursor() const { return sequencer->getCursor().get(); }

    SequenceBarrier newBarrier(const std::vector<Sequence*>& dependents = {})
    {
        return SequenceBarrier(sequencer->getWaitStrategy(), sequencer->getCursor(), dependents);
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

private:
    RingBuffer(Factory factory, std::unique_ptr<Sequencer> sequencer)
        : bufferSize(sequencer->getBufferSize()), entries(static_cast<size_t>(bufferSize)), sequencer(std::move(sequencer))
    {
        for (int i = 0; i < bufferSize; ++i)
        {
            entries[static_cast<size_t>(i)] = factory();
        }
    }

    int bufferSize;
    std::vector<T> entries;
    std::unique_ptr<Sequencer> sequencer;
};
} // namespace disruptor
