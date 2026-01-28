#pragma once

#include <atomic>
#include <climits>
#include <exception>
#include <stdexcept>
#include <thread>

#include "consumer_barrier.h"
#include "event_processor.h"
#include "exceptions.h"
#include "ring_buffer.h"
#include "sequence.h"
#include "work_handler.h"

namespace disruptor
{

/**
 * WorkProcessor: consumes events from a RingBuffer with work-queue semantics.
 * Multiple WorkProcessors share a single workSequence (CAS claim) so each
 * sequence is processed by exactly one worker.
 */
template <typename T>
class WorkProcessor final : public EventProcessor
{
public:
    WorkProcessor(RingBuffer<T>& ringBuffer, SequenceBarrier barrier, WorkHandler<T>& handler, Sequence& workSequence,
                  long endSequenceInclusive = LONG_MAX, int workBatchSize = 1)
        : ringBuffer_(ringBuffer),
          barrier_(std::move(barrier)),
          handler_(handler),
          workSequence_(workSequence),
          endSequenceInclusive_(endSequenceInclusive),
          workBatchSize_(workBatchSize)
    {
    }

    void run() override
    {
        running_.store(true, std::memory_order_release);
        barrier_.clearAlert();

        try
        {
            handler_.onStart();

            if (workBatchSize_ < 1)
            {
                throw std::invalid_argument("workBatchSize must be >= 1");
            }

            long nextSequence = 0;
            long claimedHi = -1;

            while (running_.load(std::memory_order_acquire))
            {
                try
                {
                    if (nextSequence > claimedHi)
                    {
                        // Claim a batch of sequences to reduce contention on workSequence.
                        long base = workSequence_.getAndAdd(workBatchSize_);
                        nextSequence = base + 1;
                        claimedHi = base + workBatchSize_;

                        if (nextSequence > endSequenceInclusive_)
                        {
                            sequence_.set(endSequenceInclusive_);
                            break;
                        }
                        if (claimedHi > endSequenceInclusive_)
                        {
                            claimedHi = endSequenceInclusive_;
                        }
                    }

                    long available = barrier_.waitFor(nextSequence);
                    if (available < nextSequence)
                    {
                        continue;
                    }

                    long hi = available;
                    if (hi > claimedHi)
                    {
                        hi = claimedHi;
                    }

                    // Consume the whole currently-available window in one waitFor().
                    for (; nextSequence <= hi; ++nextSequence)
                    {
                        try
                        {
                            T& evt = ringBuffer_.get(nextSequence);
                            handler_.onEvent(evt, nextSequence);
                        }
                        catch (...)
                        {
                            // Swallow handler exceptions to avoid stalling the worker pool.
                        }
                    }

                    // Update gating sequence once per chunk.
                    sequence_.set(hi);
                }
                catch (const AlertException&)
                {
                    if (!running_.load(std::memory_order_acquire))
                    {
                        break;
                    }
                }
            }

            handler_.onShutdown();
        }
        catch (...)
        {
            running_.store(false, std::memory_order_release);
            throw;
        }

        running_.store(false, std::memory_order_release);
    }

    void halt() override
    {
        running_.store(false, std::memory_order_release);
        barrier_.alert();
    }

    bool isRunning() const override
    {
        return running_.load(std::memory_order_acquire);
    }

    Sequence& getSequence() override
    {
        return sequence_;
    }

private:
    RingBuffer<T>& ringBuffer_;
    SequenceBarrier barrier_;
    WorkHandler<T>& handler_;
    Sequence& workSequence_;
    long endSequenceInclusive_ = LONG_MAX;
    int workBatchSize_ = 1;

    Sequence sequence_{Sequence::INITIAL_VALUE};
    std::atomic<bool> running_{false};
};

} // namespace disruptor
