#pragma once

#include <memory>
#include <thread>
#include <vector>

#include "ring_buffer.h"
#include "sequence.h"
#include "work_handler.h"
#include "work_processor.h"

namespace disruptor
{

/**
 * WorkerPool: convenience wrapper around multiple WorkProcessors.
 *
 * Note: does not manage thread affinity; caller may create threads manually
 * if pinning is required.
 */
template <typename T>
class WorkerPool
{
public:
    explicit WorkerPool(RingBuffer<T>& ringBuffer, const std::vector<WorkHandler<T>*>& handlers)
        : ringBuffer_(ringBuffer)
    {
        processors_.reserve(handlers.size());
        for (auto* h : handlers)
        {
            processors_.push_back(std::make_unique<WorkProcessor<T>>(ringBuffer_, ringBuffer_.newBarrier(), *h, workSequence_));
        }
    }

    std::vector<Sequence*> getWorkerSequences()
    {
        std::vector<Sequence*> seqs;
        seqs.reserve(processors_.size());
        for (auto& p : processors_)
        {
            seqs.push_back(&p->getSequence());
        }
        return seqs;
    }

    void start()
    {
        threads_.clear();
        threads_.reserve(processors_.size());
        for (auto& p : processors_)
        {
            auto* proc = p.get();
            threads_.emplace_back([proc] { proc->run(); });
        }
    }

    void halt()
    {
        for (auto& p : processors_)
        {
            p->halt();
        }
    }

    void join()
    {
        for (auto& t : threads_)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
        threads_.clear();
    }

    Sequence& getWorkSequence() { return workSequence_; }

private:
    RingBuffer<T>& ringBuffer_;
    Sequence workSequence_{Sequence::INITIAL_VALUE};
    std::vector<std::unique_ptr<WorkProcessor<T>>> processors_;
    std::vector<std::thread> threads_;
};

} // namespace disruptor
