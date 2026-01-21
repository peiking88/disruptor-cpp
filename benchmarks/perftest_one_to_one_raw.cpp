/**
 * OneToOneRawThroughputTest - C++ equivalent
 *
 * Raw throughput test without EventProcessor.
 * Uses only Sequencer and SequenceBarrier directly for maximum performance.
 *
 * Topology:
 *   +----+    +-----+
 *   | P1 |--->| EP1 |
 *   +----+    +-----+
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "disruptor/sequence.h"
#include "disruptor/consumer_barrier.h"
#include "disruptor/producer_sequencer.h"
#include "disruptor/wait_strategy.h"

int main()
{
    constexpr int bufferSize = 1024 * 64;
    constexpr long iterations = 200'000'000L;

    std::cout << "PerfTest: OneToOneRawThroughput\n";
    std::cout << "Iterations: " << iterations << "\n";

    disruptor::YieldingWaitStrategy waitStrategy;
    disruptor::SingleProducerSequencer sequencer(bufferSize, waitStrategy);

    // Consumer sequence
    disruptor::Sequence consumerSequence(disruptor::Sequence::INITIAL_VALUE);
    sequencer.addGatingSequences({&consumerSequence});

    // Create barrier directly (like Java raw test)
    disruptor::SequenceBarrier barrier(waitStrategy, sequencer.getCursor(), {});

    std::atomic<bool> done{false};

    // Consumer thread - raw waitFor loop
    std::thread consumer([&] {
        long expected = iterations - 1;
        long processed = -1;

        try
        {
            do
            {
                processed = barrier.waitFor(consumerSequence.get() + 1);
                consumerSequence.set(processed);
            } while (processed < expected);
        }
        catch (const disruptor::AlertException&)
        {
            // Interrupted
        }

        done.store(true, std::memory_order_release);
    });

    auto start = std::chrono::steady_clock::now();

    // Producer - publish sequences
    for (long i = 0; i < iterations; ++i)
    {
        long next = sequencer.next();
        sequencer.publish(next);
    }

    // Wait for consumer
    while (!done.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }

    auto end = std::chrono::steady_clock::now();

    barrier.alert();
    consumer.join();

    double elapsed = std::chrono::duration<double>(end - start).count();
    double opsPerSec = iterations / elapsed;

    std::cout << "Time(s): " << elapsed << "\n";
    std::cout << "Throughput(ops/s): " << opsPerSec << "\n";

    return 0;
}
