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
- Git (submodules: `external/Catch2`, `external/NanoLog`, `external/backward-cpp`)

> Initialize submodules before build:
> `git submodule update --init --recursive`

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

## Latest Benchmark Result (current build with exception/log integration)

- `disruptor_benchmark`: 1.34499e+08 events/s

## C++ Benchmark Results (current)

### JMH-style (C++ equivalents)

- `SingleProducerSingleConsumer`:
  - Iterations: 10,000,000
  - Time(s): 0.0439001
  - Throughput(ops/s): 2.27790e+08
  - Average(ns/op): 4.39001
- `MultiProducerSingleConsumer`:
  - Producers: 4
  - Iterations per producer: 10,000,000
  - Time(s): 0.706818
  - Throughput(ops/s): 5.65916e+07
- `Sequence` vs `std::atomic` (ns/op, ops/s):
  - Atomic get: 0.179859 ns/op, 5.55992e+09 ops/s
  - Atomic set: 0.090708 ns/op, 1.10244e+10 ops/s
  - Atomic getAndAdd: 3.50266 ns/op, 2.85497e+08 ops/s
  - Sequence get: 0.17926 ns/op, 5.57849e+09 ops/s
  - Sequence set: 0.180906 ns/op, 5.52773e+09 ops/s
  - Sequence incrementAndGet: 3.50202 ns/op, 2.8555e+08 ops/s

### PerfTest-style (C++ equivalents)

- `OneToOneSequencedThroughputTest`:
  - Iterations: 10,000,000
  - Time(s): 0.0720616
  - Throughput(ops/s): 1.38770e+08
- `ThreeToOneSequencedThroughputTest`:
  - Producers: 3
  - Iterations: 20,000,000
  - Time(s): 0.602149
  - Throughput(ops/s): 3.32143e+07

## Exception Handling & Logging

- Added `ExceptionHandler<T>` with default `FatalExceptionHandler` (logs + rethrows) and `IgnoreExceptionHandler` (logs only).
- `BatchEventProcessor` now routes uncaught handler exceptions to the configured handler and notifies `onStart/onShutdown` errors.
- Logging via NanoLog; stack traces via backward-cpp.

## Java Disruptor JMH (local run, JDK 21)

- `SingleProducerSingleConsumer.producing`: 5.8539 ns/op (≈1.71e+08 ops/s)
- `MultiProducerSingleConsumer.producing` (4 threads): 38224 ops/ms (≈3.82e+07 ops/s)

## C++ vs Java (rough, non-like-for-like)

- C++ SPSC: 4.39001 ns/op (2.2779e+08 ops/s) vs Java 5.8539 ns/op ⇒ C++ faster ~25% (ns/op basis).
- C++ MPSC (4p1c): 5.65916e+07 ops/s vs Java 3.82e+07 ops/s ⇒ C++ faster ~48%.

Notes: Different languages/runtime/harness; numbers are indicative only.

## Delta vs previous C++ baseline (pre exception/log integration)

| Benchmark | Current | Previous baseline | Change |
| --- | ---: | ---: | ---: |
| disruptor_benchmark (events/s) | 1.34499e+08 | 1.3328e+08 | **+0.9%** |
| jmh_spsc (ops/s) | 2.27790e+08 | 2.25045e+08 | **+1.2%** |
| jmh_mpsc (ops/s) | 5.65916e+07 | 5.17231e+07 | **+9.4%** |
| perf_one_to_one (ops/s) | 1.38770e+08 | 1.37592e+08 | **+0.9%** |
| perf_three_to_one (ops/s) | 3.32143e+07 | 4.3282e+07 | **-23.2%** |

Notes: 三生产者路径仍显著回落；其他项目小幅提升或持平。可对三生产者场景做异常捕获热路径的 A/B 优化验证。
