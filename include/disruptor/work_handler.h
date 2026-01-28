#pragma once

namespace disruptor
{

/**
 * Work-queue handler (MPMC): each sequence is processed by exactly one worker.
 */
template <typename T>
class WorkHandler
{
public:
    virtual ~WorkHandler() = default;

    virtual void onEvent(T& event, long sequence) = 0;

    virtual void onStart() {}
    virtual void onShutdown() {}
};

} // namespace disruptor
