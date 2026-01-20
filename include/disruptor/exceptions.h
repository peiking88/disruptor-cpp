#pragma once

#include <stdexcept>

namespace disruptor
{
class AlertException final : public std::runtime_error
{
public:
    AlertException() : std::runtime_error("Alerted") {}
};

class InsufficientCapacityException final : public std::runtime_error
{
public:
    InsufficientCapacityException() : std::runtime_error("Insufficient capacity") {}
};
} // namespace disruptor
