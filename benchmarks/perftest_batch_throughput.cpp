/**
 * High-Performance Batch Throughput Test
 * 
 * Tests various batch sizes and publishing modes:
 * 1. Standard per-event publish
 * 2. BatchPublisher Mode 1: Fixed batch size (simple API)
 * 3. BatchPublisher Mode 2: Dynamic batch (Java-style API)
 * 4. Direct RingBuffer batch (raw API)
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "disruptor/ring_buffer.h"
#include "disruptor/consumer_barrier.h"
#include "disruptor/wait_strategy.h"

// Compact event - better cache utilization for sequential access
struct ValueEvent
{
    long value = 0;
};

// Cache-line padded event - only useful when multiple threads access adjacent events
struct alignas(64) PaddedValueEvent
{
    long value = 0;
    char padding[56]{};
};

// Generic fast consumer for any event type
template <typename T>
class GenericFastConsumer
{
public:
    GenericFastConsumer(disruptor::RingBuffer<T>& ringBuffer, disruptor::SequenceBarrier& barrier)
        : ringBuffer_(ringBuffer), barrier_(barrier), 
          entries_(ringBuffer.getEntries()), indexMask_(ringBuffer.getIndexMask())
    {
    }

    void run(long expectedCount)
    {
        long nextSequence = sequence_.get() + 1;
        long count = 0;
        long sum = 0;

        try
        {
            while (count < expectedCount)
            {
                long available = barrier_.waitFor(nextSequence);
                
                for (long seq = nextSequence; seq <= available; ++seq)
                {
                    size_t index = static_cast<size_t>(seq) & indexMask_;
                    sum += entries_[index].value;
                    ++count;
                }
                
                sequence_.set(available);
                nextSequence = available + 1;
            }
        }
        catch (const disruptor::AlertException&)
        {
        }

        sum_ = sum;
        done_.store(true, std::memory_order_release);
    }

    disruptor::Sequence& getSequence() { return sequence_; }
    long getSum() const { return sum_; }
    bool isDone() const { return done_.load(std::memory_order_acquire); }

private:
    disruptor::RingBuffer<T>& ringBuffer_;
    disruptor::SequenceBarrier& barrier_;
    T* entries_;
    size_t indexMask_;
    disruptor::Sequence sequence_{disruptor::Sequence::INITIAL_VALUE};
    std::atomic<bool> done_{false};
    long sum_ = 0;
};

// Alias for ValueEvent consumer
using FastConsumer = GenericFastConsumer<ValueEvent>;

// Test setup helper
struct TestContext
{
    static constexpr int bufferSize = 1024 * 64;
    
    disruptor::YieldingWaitStrategy waitStrategy;
    disruptor::RingBuffer<ValueEvent> ringBuffer;
    disruptor::SequenceBarrier barrier;
    FastConsumer consumer;
    std::thread consumerThread;
    
    TestContext()
        : ringBuffer(disruptor::RingBuffer<ValueEvent>::createSingleProducer(
            [] { return ValueEvent{}; }, bufferSize, waitStrategy)),
          barrier(ringBuffer.newBarrier()),
          consumer(ringBuffer, barrier)
    {
        ringBuffer.addGatingSequences({&consumer.getSequence()});
    }
    
    void start(long totalEvents)
    {
        consumerThread = std::thread([this, totalEvents] { consumer.run(totalEvents); });
    }
    
    double finish(long totalEvents)
    {
        while (!consumer.isDone()) std::this_thread::yield();
        barrier.alert();
        consumerThread.join();
        
        long long expectedSum = (static_cast<long long>(totalEvents - 1) * totalEvents) / 2;
        bool sumOk = consumer.getSum() == expectedSum;
        if (!sumOk) std::cout << " [SUM ERROR!]";
        return 0;
    }
};

// Test 1: Standard per-event publishing
void runStandardTest(long totalEvents)
{
    TestContext ctx;
    ctx.start(totalEvents);
    
    auto start = std::chrono::steady_clock::now();
    for (long i = 0; i < totalEvents; ++i)
    {
        long seq = ctx.ringBuffer.next();
        ctx.ringBuffer.get(seq).value = i;
        ctx.ringBuffer.publish(seq);
    }
    auto end = std::chrono::steady_clock::now();
    ctx.finish(totalEvents);
    
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  Standard (per-event):    " << totalEvents / elapsed << " events/s\n";
}

// Test 2: BatchPublisher Mode 1 - Fixed batch size
void runBatchMode1Test(int batchSize, long totalEvents)
{
    TestContext ctx;
    ctx.start(totalEvents);
    
    auto start = std::chrono::steady_clock::now();
    auto publisher = ctx.ringBuffer.createBatchPublisher(batchSize);
    for (long i = 0; i < totalEvents; ++i)
    {
        auto& event = publisher.claim();
        event.value = i;
        if (publisher.isFull()) publisher.publishBatch();
    }
    publisher.publishBatch();
    auto end = std::chrono::steady_clock::now();
    ctx.finish(totalEvents);
    
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  Mode1 Fixed (batch=" << batchSize << "):  " << totalEvents / elapsed << " events/s\n";
}

// Test 3: BatchPublisher Mode 2 - Dynamic batch (Java-style)
void runBatchMode2Test(int batchSize, long totalEvents)
{
    TestContext ctx;
    ctx.start(totalEvents);
    
    auto start = std::chrono::steady_clock::now();
    auto publisher = ctx.ringBuffer.createBatchPublisher();
    long remaining = totalEvents;
    long valueCounter = 0;
    
    while (remaining > 0)
    {
        int chunk = static_cast<int>(std::min<long>(remaining, batchSize));
        publisher.beginBatch(chunk);
        for (int i = 0; i < chunk; ++i)
        {
            publisher.getEvent(i).value = valueCounter++;
        }
        publisher.endBatch();
        remaining -= chunk;
    }
    auto end = std::chrono::steady_clock::now();
    ctx.finish(totalEvents);
    
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  Mode2 Dynamic (batch=" << batchSize << "): " << totalEvents / elapsed << " events/s\n";
}

// Test 4: Direct RingBuffer batch (raw API, like Java)
void runDirectBatchTest(int batchSize, long totalEvents)
{
    TestContext ctx;
    ctx.start(totalEvents);
    
    auto start = std::chrono::steady_clock::now();
    long remaining = totalEvents;
    long valueCounter = 0;
    
    while (remaining > 0)
    {
        int chunk = static_cast<int>(std::min<long>(remaining, batchSize));
        long hi = ctx.ringBuffer.next(chunk);
        long lo = hi - chunk + 1;
        for (long seq = lo; seq <= hi; ++seq)
        {
            ctx.ringBuffer.get(seq).value = valueCounter++;
        }
        ctx.ringBuffer.publish(lo, hi);
        remaining -= chunk;
    }
    auto end = std::chrono::steady_clock::now();
    ctx.finish(totalEvents);
    
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "  Direct API (batch=" << batchSize << "):    " << totalEvents / elapsed << " events/s\n";
}

int main()
{
    constexpr long totalEvents = 100'000'000L;

    std::cout << "=== High-Performance Batch Throughput Test ===\n";
    std::cout << "Total Events: " << totalEvents << "\n\n";

    // Warmup
    std::cout << "Warmup...\n";
    runDirectBatchTest(100, 10'000'000L);
    std::cout << "\n";

    // Baseline
    std::cout << "--- 1. Baseline (per-event) ---\n";
    runStandardTest(totalEvents);
    std::cout << "\n";

    // Mode 1: Fixed batch
    std::cout << "--- 2. BatchPublisher Mode 1 (Fixed batch) ---\n";
    runBatchMode1Test(10, totalEvents);
    runBatchMode1Test(100, totalEvents);
    runBatchMode1Test(500, totalEvents);
    std::cout << "\n";

    // Mode 2: Dynamic batch (Java-style)
    std::cout << "--- 3. BatchPublisher Mode 2 (Dynamic, Java-style) ---\n";
    runBatchMode2Test(10, totalEvents);
    runBatchMode2Test(100, totalEvents);
    runBatchMode2Test(500, totalEvents);
    std::cout << "\n";

    // Direct API
    std::cout << "--- 4. Direct RingBuffer API (raw) ---\n";
    runDirectBatchTest(10, totalEvents);
    runDirectBatchTest(100, totalEvents);
    runDirectBatchTest(500, totalEvents);
    std::cout << "\n";

    // Summary
    std::cout << "=== Summary ===\n";
    std::cout << "Mode 1 (Fixed): Simple API, good for streaming data\n";
    std::cout << "Mode 2 (Dynamic): Flexible batch size, Java-compatible\n";
    std::cout << "Direct API: Minimal overhead, maximum control\n\n";

    // Compare compact vs padded events
    std::cout << "--- 5. Event Size Comparison (batch=100) ---\n";
    std::cout << "  sizeof(ValueEvent)=" << sizeof(ValueEvent) << " bytes\n";
    std::cout << "  sizeof(PaddedValueEvent)=" << sizeof(PaddedValueEvent) << " bytes\n";
    
    // Test with compact events (already done above)
    std::cout << "  Compact Event:  see Mode2 batch=100 above\n";
    
    // Test with padded events
    {
        constexpr int bufferSize = 1024 * 64;
        disruptor::YieldingWaitStrategy waitStrategy;
        auto ringBuffer = disruptor::RingBuffer<PaddedValueEvent>::createSingleProducer(
            [] { return PaddedValueEvent{}; }, bufferSize, waitStrategy);
        auto barrier = ringBuffer.newBarrier();
        GenericFastConsumer<PaddedValueEvent> consumer(ringBuffer, barrier);
        ringBuffer.addGatingSequences({&consumer.getSequence()});
        
        std::thread consumerThread([&] { consumer.run(totalEvents); });
        
        auto start = std::chrono::steady_clock::now();
        auto publisher = ringBuffer.createBatchPublisher();
        long remaining = totalEvents;
        long valueCounter = 0;
        while (remaining > 0) {
            int chunk = static_cast<int>(std::min<long>(remaining, 100));
            publisher.beginBatch(chunk);
            for (int i = 0; i < chunk; ++i) {
                publisher.getEvent(i).value = valueCounter++;
            }
            publisher.endBatch();
            remaining -= chunk;
        }
        auto end = std::chrono::steady_clock::now();
        
        while (!consumer.isDone()) std::this_thread::yield();
        barrier.alert();
        consumerThread.join();
        
        double elapsed = std::chrono::duration<double>(end - start).count();
        std::cout << "  Padded Event:   " << totalEvents / elapsed << " events/s\n";
    }
    std::cout << "\nNote: Padded events hurt performance due to reduced cache utilization.\n";
    std::cout << "Use padding only for Sequence (shared state), not for event data.\n";

    return 0;
}
