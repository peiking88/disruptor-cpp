# disruptor-cpp

A high-performance C++23 implementation of the Disruptor pattern.

## Key Features

- Ring buffer with preallocated events and power-of-two sizing
- Single-producer and multi-producer publishing modes
- Flexible consumer topologies: broadcast, pipeline, dependency graph
- Multiple wait strategies (blocking, busy-spin, yielding, sleeping)
- Cache-line padding to prevent false sharing
- **Single-threaded throughput: 439 million ops/s** (164% faster than Java)
- **Batch throughput: 1.45 billion events/s**
- **Ultra-low latency: P50 = 90ns, P99 = 150ns**

## Quick Start

### Build

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Run Tests & Benchmarks

```bash
./build/disruptor_tests              # 101 test cases, 252 assertions
./build/disruptor_perf_one_to_one    # Basic throughput test
./build/disruptor_perf_ping_pong     # Latency test
```

## Performance Guide

### 1. Choose the Right Publishing Mode

```cpp
// BEST: Batch publishing with Mode2 Dynamic (4.23e9 events/s)
auto publisher = ringBuffer.createBatchPublisher();
publisher.beginBatch(100);
for (int i = 0; i < 100; ++i) {
    publisher.getEvent(i).value = data[i];
}
publisher.endBatch();

// GOOD: Standard batch via RingBuffer API
long hi = ringBuffer.next(n);
long lo = hi - n + 1;
for (long seq = lo; seq <= hi; ++seq) {
    ringBuffer.get(seq).value = ...;
}
ringBuffer.publish(lo, hi);

// OK: Per-event publishing (3.1e8 events/s)
long seq = ringBuffer.next();
ringBuffer.get(seq).value = ...;
ringBuffer.publish(seq);
```

### 2. Choose the Right Wait Strategy

| Strategy | Use Case | Latency | CPU |
|----------|----------|---------|-----|
| `BusySpinWaitStrategy` | Ultra-low latency | **Best** | 100% |
| `YieldingWaitStrategy` | Low latency | Good | Medium |
| `SleepingWaitStrategy` | Balanced | Medium | Low |
| `BlockingWaitStrategy` | Throughput-focused | Higher | **Lowest** |

```cpp
// For latency-sensitive applications
disruptor::BusySpinWaitStrategy waitStrategy;

// For balanced workloads
disruptor::YieldingWaitStrategy waitStrategy;
```

### 3. Keep Events Compact (Don't Pad Events!)

```cpp
// GOOD: Compact events maximize cache utilization
struct Event {
    long value;
    int type;
};  // 16 bytes - 4 events per cache line

// BAD: Padded events waste cache
struct alignas(64) PaddedEvent {
    long value;
};  // 64 bytes - performance drops 57%!
```

**Note**: Cache-line padding is already applied to `Sequence` internally. User events should remain compact.

### 4. Use Direct Array Access for Consumers

```cpp
// High-performance consumer pattern
T* entries = ringBuffer.getEntries();
size_t mask = ringBuffer.getIndexMask();

while (count < expected) {
    long available = barrier.waitFor(nextSeq);
    for (long seq = nextSeq; seq <= available; ++seq) {
        // Direct index calculation - no function call
        T& event = entries[seq & mask];
        process(event);
    }
    sequence.set(available);
    nextSeq = available + 1;
}
```

### 5. Producer Selection

| Scenario | Sequencer | Notes |
|----------|-----------|-------|
| Single thread producing | `SingleProducerSequencer` | **Fastest**, no CAS |
| Multiple threads producing | `MultiProducerSequencer` | Thread-safe |

```cpp
// Single producer (recommended when possible)
auto rb = RingBuffer<Event>::createSingleProducer(factory, 65536, waitStrategy);

// Multi producer
auto rb = RingBuffer<Event>::createMultiProducer(factory, 65536, waitStrategy);
```

### 6. Buffer Size Guidelines

- Use power-of-two sizes: 1024, 4096, 65536, etc.
- Larger buffers absorb bursts but increase memory
- Recommended: `64 * 1024` for most use cases

## Performance Results

### Throughput (100M events)

| Test | Throughput | Notes |
|------|------------|-------|
| **OneToOne (optimized)** | **4.39e8 ops/s** | FastEventHandler + BusySpinWait |
| **OneToOneBatch (100)** | **1.45e9 ops/s** | Batch publishing |
| OneToThree (broadcast) | 1.02e8 ops/s | 3 consumers |
| ThreeToOne | 3.76e7 ops/s | 3 producers |
| ThreeToThree | 3.04e7 ops/s | 3 producers, 3 consumers |

### Latency (Ping-Pong)

| Metric | C++ | Java | Improvement |
|--------|-----|------|-------------|
| P50 | 90 ns | 2,757 ns | **31x faster** |
| P99 | 150 ns | 7,925 ns | **53x faster** |
| P99.9 | 180 ns | 1.7 ms | **9,659x faster** |
| Max | 6 μs | 3.3 ms | **546x faster** |

### vs Java Disruptor

| Scenario | C++ | Java | Winner |
|----------|-----|------|--------|
| **Single-threaded (OneToOne)** | **4.39e8 ops/s** | 1.66e8 ops/s | **C++ +164%** |
| Broadcast (1:3) | 1.02e8 ops/s | 7.45e7 ops/s | **C++ +37%** |
| Multi-producer (3:1) | 3.76e7 ops/s | 3.55e7 ops/s | **C++ +6%** |
| ThreeToThree (3:3) | 3.04e7 ops/s | 1.09e7 ops/s | **C++ +179%** |
| Latency P50 | 90 ns | 2,757 ns | **C++ 31x faster** |
| Latency P99 | 150 ns | 7,925 ns | **C++ 53x faster** |

## Header Files

| File | Description |
|------|-------------|
| `ring_buffer.h` | Ring buffer and BatchPublisher |
| `producer_sequencer.h` | Single/Multi producer sequencers |
| `consumer_barrier.h` | Consumer wait barrier |
| `sequence.h` | Cache-padded sequence counter |
| `wait_strategy.h` | Wait strategy implementations |
| `batch_event_processor.h` | Event processor with batching |
| `event_handler.h` | Event handler interfaces |
| `cache_line_storage.h` | Generic cache-line padding template |

## Requirements

- C++23 compiler (GCC 13+ / Clang 16+)
- CMake 3.20+
- Git (for submodules)

## License

Apache 2.0 License
