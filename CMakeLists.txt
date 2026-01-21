cmake_minimum_required(VERSION 3.20)
project(disruptor-cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  add_compile_options(/W0)
else()
  add_compile_options(-w)
endif()

add_library(disruptor INTERFACE)

add_subdirectory(external/backward-cpp EXCLUDE_FROM_ALL)

add_library(nanolog STATIC
  external/NanoLog/runtime/Cycles.cc
  external/NanoLog/runtime/Util.cc
  external/NanoLog/runtime/Log.cc
  external/NanoLog/runtime/NanoLog.cc
  external/NanoLog/runtime/RuntimeLogger.cc
  external/NanoLog/runtime/TimeTrace.cc
  external/NanoLog/runtime/GeneratedFunctionsStub.cc
)

target_include_directories(nanolog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/external/NanoLog/runtime)
target_link_libraries(nanolog PUBLIC rt pthread)

target_include_directories(disruptor INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/external/NanoLog/runtime
  ${CMAKE_CURRENT_SOURCE_DIR}/external/backward-cpp
)

target_link_libraries(disruptor INTERFACE nanolog Backward::Interface)

option(DISRUPTOR_BUILD_TESTS "Build unit tests" ON)
option(DISRUPTOR_BUILD_BENCHMARKS "Build benchmarks" ON)

if (DISRUPTOR_BUILD_TESTS)
  add_subdirectory(external/Catch2 EXCLUDE_FROM_ALL)

  add_executable(disruptor_tests
    tests/test_sequence.cpp
    tests/test_single_producer.cpp
    tests/test_multi_producer.cpp
    tests/test_exception_handler.cpp
    tests/test_stress_multi_producer.cpp
    tests/test_ring_buffer.cpp
    tests/test_consumer_barrier.cpp
    tests/test_wait_strategy.cpp
    tests/test_producer_sequencer.cpp
    tests/test_batch_event_processor.cpp
    tests/test_util.cpp
  )
  target_link_libraries(disruptor_tests PRIVATE disruptor Catch2::Catch2WithMain)
  enable_testing()
  add_test(NAME disruptor_tests COMMAND disruptor_tests)
endif()

if (DISRUPTOR_BUILD_BENCHMARKS)
  add_executable(disruptor_benchmark benchmarks/throughput.cpp)
  add_executable(disruptor_jmh_spsc benchmarks/jmh_single_producer.cpp)
  add_executable(disruptor_jmh_mpsc benchmarks/jmh_multi_producer.cpp)
  add_executable(disruptor_jmh_sequence benchmarks/jmh_sequence.cpp)
  add_executable(disruptor_perf_one_to_one benchmarks/perftest_one_to_one.cpp)
  add_executable(disruptor_perf_one_to_one_batch benchmarks/perftest_one_to_one_batch.cpp)
  add_executable(disruptor_perf_one_to_three benchmarks/perftest_one_to_three.cpp)
  add_executable(disruptor_perf_one_to_three_pipeline benchmarks/perftest_one_to_three_pipeline.cpp)
  add_executable(disruptor_perf_one_to_three_diamond benchmarks/perftest_one_to_three_diamond.cpp)
  add_executable(disruptor_perf_three_to_one benchmarks/perftest_three_to_one.cpp)
  add_executable(disruptor_perf_three_to_one_batch benchmarks/perftest_three_to_one_batch.cpp)
  add_executable(disruptor_perf_three_to_three benchmarks/perftest_three_to_three.cpp)
  add_executable(disruptor_perf_ping_pong benchmarks/perftest_ping_pong_latency.cpp)
  add_executable(disruptor_perf_one_to_one_raw benchmarks/perftest_one_to_one_raw.cpp)
  add_executable(disruptor_benchmark_analysis benchmarks/benchmark_analysis.cpp)
  add_executable(disruptor_benchmark_deep benchmarks/benchmark_deep_analysis.cpp)
  add_executable(disruptor_perf_batch_throughput benchmarks/perftest_batch_throughput.cpp)

  target_link_libraries(disruptor_benchmark PRIVATE disruptor)
  target_link_libraries(disruptor_jmh_spsc PRIVATE disruptor)
  target_link_libraries(disruptor_jmh_mpsc PRIVATE disruptor)
  target_link_libraries(disruptor_jmh_sequence PRIVATE disruptor)
  target_link_libraries(disruptor_perf_one_to_one PRIVATE disruptor)
  target_link_libraries(disruptor_perf_one_to_one_batch PRIVATE disruptor)
  target_link_libraries(disruptor_perf_one_to_three PRIVATE disruptor)
  target_link_libraries(disruptor_perf_one_to_three_pipeline PRIVATE disruptor)
  target_link_libraries(disruptor_perf_one_to_three_diamond PRIVATE disruptor)
  target_link_libraries(disruptor_perf_three_to_one PRIVATE disruptor)
  target_link_libraries(disruptor_perf_three_to_one_batch PRIVATE disruptor)
  target_link_libraries(disruptor_perf_three_to_three PRIVATE disruptor)
  target_link_libraries(disruptor_perf_ping_pong PRIVATE disruptor)
  target_link_libraries(disruptor_perf_one_to_one_raw PRIVATE disruptor)
  target_link_libraries(disruptor_benchmark_analysis PRIVATE disruptor)
  target_link_libraries(disruptor_benchmark_deep PRIVATE disruptor)
  target_link_libraries(disruptor_perf_batch_throughput PRIVATE disruptor)
endif()
