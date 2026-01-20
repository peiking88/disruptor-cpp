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
- Submodules: `external/NanoLog`, `external/backward-cpp` (logging & stack trace for exception handling)

> Note: Catch2 is still fetched via CMake FetchContent. Initialize submodules before build:
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

- `disruptor_benchmark`: 1.61603e+08 events/s

## C++ Benchmark Results (current)

### JMH-style (C++ equivalents)

- `SingleProducerSingleConsumer`:
  - Iterations: 10,000,000
  - Time(s): 0.0445689
  - Throughput(ops/s): 2.24372e+08
  - Average(ns/op): 4.45689
- `MultiProducerSingleConsumer`:
  - Producers: 4
  - Iterations per producer: 10,000,000
  - Time(s): 0.726736
  - Throughput(ops/s): 5.50406e+07
- `Sequence` vs `std::atomic` (ns/op, ops/s):
  - Atomic get: 0.183117 ns/op, 5.46098e+09 ops/s
  - Atomic set: 0.0920509 ns/op, 1.08636e+10 ops/s
  - Atomic getAndAdd: 3.50078 ns/op, 2.8565e+08 ops/s
  - Sequence get: 0.178477 ns/op, 5.60295e+09 ops/s
  - Sequence set: 0.179053 ns/op, 5.58493e+09 ops/s
  - Sequence incrementAndGet: 3.47828 ns/op, 2.87498e+08 ops/s

### PerfTest-style (C++ equivalents)

- `OneToOneSequencedThroughputTest`:
  - Iterations: 10,000,000
  - Time(s): 0.0728422
  - Throughput(ops/s): 1.37283e+08
- `ThreeToOneSequencedThroughputTest`:
  - Producers: 3
  - Iterations: 20,000,000
  - Time(s): 0.552405
  - Throughput(ops/s): 3.62053e+07

## Exception Handling & Logging

- Added `ExceptionHandler<T>` with default `FatalExceptionHandler` (logs + rethrows) and `IgnoreExceptionHandler` (logs only).
- `BatchEventProcessor` now routes uncaught handler exceptions to the configured handler and notifies `onStart/onShutdown` errors.
- Logging via NanoLog; stack traces via backward-cpp.

## Java Disruptor JMH (local run, JDK 21)

- `SingleProducerSingleConsumer.producing`: 5.8539 ns/op (≈1.71e+08 ops/s)
- `MultiProducerSingleConsumer.producing` (4 threads): 38224 ops/ms (≈3.82e+07 ops/s)

## C++ vs Java (rough, non-like-for-like)

- C++ SPSC: 4.45689 ns/op (2.24e+08 ops/s) vs Java 5.8539 ns/op ⇒ C++ faster ~31% (ns/op basis).
- C++ MPSC (4p1c): 5.50406e+07 ops/s vs Java 3.82e+07 ops/s ⇒ C++ faster ~44%.

Notes: Different languages/runtime/harness; numbers are indicative only.

## Delta vs previous C++ baseline (pre exception/log integration)

| Benchmark | Current | Previous baseline | Change |
| --- | ---: | ---: | ---: |
| disruptor_benchmark (events/s) | 1.61603e+08 | 1.3328e+08 | **+21.3%** |
| jmh_spsc (ops/s) | 2.24372e+08 | 2.25045e+08 | **-0.3%** |
| jmh_mpsc (ops/s) | 5.50406e+07 | 5.17231e+07 | **+6.4%** |
| perf_one_to_one (ops/s) | 1.37283e+08 | 1.37592e+08 | **-0.2%** |
| perf_three_to_one (ops/s) | 3.62053e+07 | 4.3282e+07 | **-16.4%** |

Notes: Three-producer path regressed; others improved or flat. Three-producer regression likely due to added exception-handling hot-path overhead and MPSC contention; consider A/B with lightweight handler or bypass catch in fast path if needed.
