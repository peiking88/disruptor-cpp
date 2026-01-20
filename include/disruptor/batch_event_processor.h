#pragma once

#include <atomic>
#include <thread>

#include "event_handler.h"
#include "event_processor.h"
#include "ring_buffer.h"
#include "sequence.h"
#include "sequence_barrier.h"

namespace disruptor
{
template <typename T>
class BatchEventProcessor final : public EventProcessor
{
public:
    BatchEventProcessor(RingBuffer<T>& ringBuffer, SequenceBarrier& barrier, EventHandler<T>& handler)
        : ringBuffer(ringBuffer), barrier(barrier), handler(handler)
    {
    }

    void run() override
    {
        running.store(true, std::memory_order_release);
        long nextSequence = sequence.get() + 1;

        while (running.load(std::memory_order_acquire))
        {
            try
            {
                long available = barrier.waitFor(nextSequence);
                for (long seq = nextSequence; seq <= available; ++seq)
                {
                    handler.onEvent(ringBuffer.get(seq), seq, seq == available);
                }
                sequence.set(available);
                nextSequence = available + 1;
            }
            catch (const AlertException&)
            {
                if (!running.load(std::memory_order_acquire))
                {
                    break;
                }
            }
        }
    }

    void halt() override
    {
        running.store(false, std::memory_order_release);
        barrier.alert();
    }

    bool isRunning() const override
    {
        return running.load(std::memory_order_acquire);
    }

    Sequence& getSequence() override
    {
        return sequence;
    }

private:
    RingBuffer<T>& ringBuffer;
    SequenceBarrier& barrier;
    EventHandler<T>& handler;
    Sequence sequence{Sequence::INITIAL_VALUE};
    std::atomic<bool> running{false};
};
} // namespace disruptor
