#pragma once

namespace disruptor
{
class Sequence;

class EventProcessor
{
public:
    virtual ~EventProcessor() = default;
    virtual void run() = 0;
    virtual void halt() = 0;
    virtual bool isRunning() const = 0;
    virtual Sequence& getSequence() = 0;
};
} // namespace disruptor
