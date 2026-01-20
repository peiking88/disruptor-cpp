#pragma once

#include <atomic>
#include <exception>
#include <thread>

#include "event_handler.h"
#include "event_processor.h"
#include "exception_handler.h"
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

    void setExceptionHandler(ExceptionHandler<T>& handler)
    {
        exceptionHandler = &handler;
    }

    void run() override
    {
        running.store(true, std::memory_order_release);
        barrier.clearAlert();
        notifyStart();

        try
        {
            long nextSequence = sequence.get() + 1;
            T* event = nullptr;

            while (running.load(std::memory_order_acquire))
            {
                try
                {
                    long available = barrier.waitFor(nextSequence);
                    for (long seq = nextSequence; seq <= available; ++seq)
                    {
                        event = &ringBuffer.get(seq);
                        handler.onEvent(*event, seq, seq == available);
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
                catch (...)
                {
                    handleEventException(std::current_exception(), nextSequence, event);
                    sequence.set(nextSequence);
                    ++nextSequence;
                }
            }
        }
        catch (...)
        {
            notifyShutdown();
            running.store(false, std::memory_order_release);
            throw;
        }

        notifyShutdown();
        running.store(false, std::memory_order_release);
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
    void notifyStart()
    {
        try
        {
            handler.onStart();
        }
        catch (...)
        {
            getExceptionHandler().handleOnStartException(std::current_exception());
        }
    }

    void notifyShutdown()
    {
        try
        {
            handler.onShutdown();
        }
        catch (...)
        {
            getExceptionHandler().handleOnShutdownException(std::current_exception());
        }
    }

    void handleEventException(std::exception_ptr exception, long sequence, T* event)
    {
        getExceptionHandler().handleEventException(exception, sequence, event);
    }

    ExceptionHandler<T>& getExceptionHandler()
    {
        return exceptionHandler ? *exceptionHandler : ExceptionHandlers<T>::defaultHandler();
    }

    RingBuffer<T>& ringBuffer;
    SequenceBarrier& barrier;
    EventHandler<T>& handler;
    ExceptionHandler<T>* exceptionHandler{nullptr};
    Sequence sequence{Sequence::INITIAL_VALUE};
    std::atomic<bool> running{false};
};
} // namespace disruptor
