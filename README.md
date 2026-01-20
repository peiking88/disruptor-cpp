# disruptor-cpp

## Project Overview

`disruptor-cpp` is a C++23 implementation of the Disruptor pattern, inspired by the Java Disruptor. It delivers a high-performance event exchange framework with low latency and high throughput, based on the design and API semantics documented in `PROJECT_DOC.md`.

## Key Features

- Ring buffer with preallocated events and power-of-two sizing.
- Single-producer and multi-producer publishing modes.
- Flexible consumer topologies: single consumer, multicast, pipeline, and dependency graph (fan-out/fan-in).
- Multiple wait strategies (blocking, busy-spin, yielding, sleeping).
- False-sharing avoidance in core sequence structures.

## Dependencies & Installation

- CMake >= 3.20
- C++23 compiler (e.g., GCC 13+ / Clang 16+)
- Git (for FetchContent to pull Catch2 during test builds)

> Note: The repository also contains `external/` for third-party sources, but the current build pulls Catch2 via CMake FetchContent.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Run Unit Tests

```bash
./build/disruptor_tests
```

## Run Performance Benchmark

```bash
./build/disruptor_benchmark
```

## Latest Benchmark Result

- Events: 1,000,000
- Time(s): 0.007734
- Throughput(events/s): 1.29299e+08

## C++ Benchmark Results (Java-style coverage)

### JMH-style (C++ equivalents)

- `SingleProducerSingleConsumer`:
  - Iterations: 10,000,000
  - Time(s): 0.0441363
  - Throughput(ops/s): 2.26571e+08
  - Average(ns/op): 4.41363
- `MultiProducerSingleConsumer`:
  - Producers: 4
  - Iterations per producer: 10,000,000
  - Time(s): 0.81563
  - Throughput(ops/s): 4.90418e+07
- `Sequence` vs `std::atomic` (ns/op, ops/s):
  - Atomic get: 0.191127 ns/op, 5.23213e+09 ops/s
  - Atomic set: 0.0907619 ns/op, 1.10178e+10 ops/s
  - Atomic getAndAdd: 3.55653 ns/op, 2.81173e+08 ops/s
  - Sequence get: 0.27508 ns/op, 3.6353e+09 ops/s
  - Sequence set: 0.193371 ns/op, 5.17141e+09 ops/s
  - Sequence incrementAndGet: 3.62832 ns/op, 2.7561e+08 ops/s

### PerfTest-style (C++ equivalents)

- `OneToOneSequencedThroughputTest`:
  - Iterations: 10,000,000
  - Time(s): 0.073474
  - Throughput(ops/s): 1.36103e+08
- `ThreeToOneSequencedThroughputTest`:
  - Producers: 3
  - Iterations: 20,000,000
  - Time(s): 0.471294
  - Throughput(ops/s): 4.24364e+07

## Java Disruptor Benchmark Reference (from upstream docs)

The upstream Java Disruptor documentation reports:

- **>25 million messages/sec** and **latencies < 50 ns** on moderate clock-rate processors.
- **~8x higher throughput** than equivalent queue-based approaches in a three-stage pipeline test.

## C++ vs Java (High-level, non-like-for-like)

Using the upstream reference of **25M msgs/s** as a rough baseline:

- C++ `SingleProducerSingleConsumer`: **2.26571e+08 ops/s** about **9.1x** the 25M baseline.
- C++ `OneToOneSequencedThroughputTest`: **1.36103e+08 ops/s** about **5.4x** the 25M baseline.

### Comparison Notes

- The Java numbers come from the LMAX Disruptor documentation and are **not a like-for-like comparison** (different workloads, hardware, JVM settings, and test harnesses).
- The C++ results above were collected from local runs and are intended as like-scope counterparts to the Java JMH/perftest patterns.
