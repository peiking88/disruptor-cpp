cmake_minimum_required(VERSION 3.20)
project(disruptor-cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(disruptor INTERFACE)
target_include_directories(disruptor INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

option(DISRUPTOR_BUILD_TESTS "Build unit tests" ON)
option(DISRUPTOR_BUILD_BENCHMARKS "Build benchmarks" ON)

if (DISRUPTOR_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
  )
  FetchContent_MakeAvailable(Catch2)

  add_executable(disruptor_tests
    tests/test_sequence.cpp
    tests/test_single_producer.cpp
    tests/test_multi_producer.cpp
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
  add_executable(disruptor_perf_three_to_one benchmarks/perftest_three_to_one.cpp)

  target_link_libraries(disruptor_benchmark PRIVATE disruptor)
  target_link_libraries(disruptor_jmh_spsc PRIVATE disruptor)
  target_link_libraries(disruptor_jmh_mpsc PRIVATE disruptor)
  target_link_libraries(disruptor_jmh_sequence PRIVATE disruptor)
  target_link_libraries(disruptor_perf_one_to_one PRIVATE disruptor)
  target_link_libraries(disruptor_perf_three_to_one PRIVATE disruptor)
endif()
