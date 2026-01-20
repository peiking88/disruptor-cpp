#pragma once

#include <algorithm>
#include <climits>
#include <vector>

#include "sequence.h"

namespace disruptor
{
inline long getMinimumSequence(const std::vector<Sequence*>& sequences, long defaultValue)
{
    if (sequences.empty())
    {
        return defaultValue;
    }

    long minimum = LONG_MAX;
    for (auto* seq : sequences)
    {
        minimum = std::min(minimum, seq->get());
    }
    return minimum;
}
} // namespace disruptor
