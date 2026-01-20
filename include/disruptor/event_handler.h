#pragma once

namespace disruptor
{
template <typename T>
class EventHandler
{
public:
    virtual ~EventHandler() = default;
    virtual void onEvent(T& event, long sequence, bool endOfBatch) = 0;
};
} // namespace disruptor
