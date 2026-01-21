#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "disruptor/exceptions.h"
#include "disruptor/sequence.h"
#include "disruptor/wait_strategy.h"

// 各种 WaitStrategy 测试

// ========== BusySpinWaitStrategyTest ==========
TEST_CASE("BusySpinWaitStrategy should wait until sequence available", "[wait_strategy]")
{
    disruptor::BusySpinWaitStrategy strategy;
    disruptor::Sequence cursor(disruptor::Sequence::INITIAL_VALUE);
    std::atomic<bool> alerted{false};
    std::vector<disruptor::Sequence*> dependents;

    std::thread producer([&cursor] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        cursor.set(5);
    });

    long result = strategy.waitFor(5, cursor, dependents, alerted);
    REQUIRE(result >= 5);

    producer.join();
}

TEST_CASE("BusySpinWaitStrategy should throw on alert", "[wait_strategy]")
{
    disruptor::BusySpinWaitStrategy strategy;
    disruptor::Sequence cursor(disruptor::Sequence::INITIAL_VALUE);
    std::atomic<bool> alerted{false};
    std::vector<disruptor::Sequence*> dependents;

    std::thread alerter([&alerted] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        alerted.store(true, std::memory_order_release);
    });

    REQUIRE_THROWS_AS(strategy.waitFor(100, cursor, dependents, alerted), disruptor::AlertException);

    alerter.join();
}

TEST_CASE("BusySpinWaitStrategy signalAllWhenBlocking should be no-op", "[wait_strategy]")
{
    disruptor::BusySpinWaitStrategy strategy;
    strategy.signalAllWhenBlocking();  // 不应该抛出异常
    REQUIRE(true);
}

// ========== YieldingWaitStrategyTest ==========
TEST_CASE("YieldingWaitStrategy should wait until sequence available", "[wait_strategy]")
{
    disruptor::YieldingWaitStrategy strategy;
    disruptor::Sequence cursor(disruptor::Sequence::INITIAL_VALUE);
    std::atomic<bool> alerted{false};
    std::vector<disruptor::Sequence*> dependents;

    std::thread producer([&cursor] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        cursor.set(3);
    });

    long result = strategy.waitFor(3, cursor, dependents, alerted);
    REQUIRE(result >= 3);

    producer.join();
}

TEST_CASE("YieldingWaitStrategy should throw on alert", "[wait_strategy]")
{
    disruptor::YieldingWaitStrategy strategy;
    disruptor::Sequence cursor(disruptor::Sequence::INITIAL_VALUE);
    std::atomic<bool> alerted{false};
    std::vector<disruptor::Sequence*> dependents;

    std::thread alerter([&alerted] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        alerted.store(true, std::memory_order_release);
    });

    REQUIRE_THROWS_AS(strategy.waitFor(100, cursor, dependents, alerted), disruptor::AlertException);

    alerter.join();
}

TEST_CASE("YieldingWaitStrategy should yield between checks", "[wait_strategy]")
{
    disruptor::YieldingWaitStrategy strategy;
    disruptor::Sequence cursor(disruptor::Sequence::INITIAL_VALUE);
    std::atomic<bool> alerted{false};
    std::vector<disruptor::Sequence*> dependents;

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&cursor] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        cursor.set(0);
    });

    strategy.waitFor(0, cursor, dependents, alerted);

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // 应该等待至少 15ms（给一些容差）
    REQUIRE(duration.count() >= 15);

    producer.join();
}

// ========== SleepingWaitStrategyTest ==========
TEST_CASE("SleepingWaitStrategy should wait until sequence available", "[wait_strategy]")
{
    disruptor::SleepingWaitStrategy strategy;
    disruptor::Sequence cursor(disruptor::Sequence::INITIAL_VALUE);
    std::atomic<bool> alerted{false};
    std::vector<disruptor::Sequence*> dependents;

    std::thread producer([&cursor] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        cursor.set(2);
    });

    long result = strategy.waitFor(2, cursor, dependents, alerted);
    REQUIRE(result >= 2);

    producer.join();
}

TEST_CASE("SleepingWaitStrategy should throw on alert", "[wait_strategy]")
{
    disruptor::SleepingWaitStrategy strategy;
    disruptor::Sequence cursor(disruptor::Sequence::INITIAL_VALUE);
    std::atomic<bool> alerted{false};
    std::vector<disruptor::Sequence*> dependents;

    std::thread alerter([&alerted] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        alerted.store(true, std::memory_order_release);
    });

    REQUIRE_THROWS_AS(strategy.waitFor(100, cursor, dependents, alerted), disruptor::AlertException);

    alerter.join();
}

TEST_CASE("SleepingWaitStrategy should sleep between checks", "[wait_strategy]")
{
    disruptor::SleepingWaitStrategy strategy;
    disruptor::Sequence cursor(disruptor::Sequence::INITIAL_VALUE);
    std::atomic<bool> alerted{false};
    std::vector<disruptor::Sequence*> dependents;

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&cursor] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        cursor.set(0);
    });

    strategy.waitFor(0, cursor, dependents, alerted);

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    REQUIRE(duration.count() >= 25);

    producer.join();
}

// ========== BlockingWaitStrategyTest ==========
TEST_CASE("BlockingWaitStrategy should wait until sequence available", "[wait_strategy]")
{
    disruptor::BlockingWaitStrategy strategy;
    disruptor::Sequence cursor(disruptor::Sequence::INITIAL_VALUE);
    std::atomic<bool> alerted{false};
    std::vector<disruptor::Sequence*> dependents;

    std::thread producer([&cursor, &strategy] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        cursor.set(1);
        strategy.signalAllWhenBlocking();
    });

    long result = strategy.waitFor(1, cursor, dependents, alerted);
    REQUIRE(result >= 1);

    producer.join();
}

TEST_CASE("BlockingWaitStrategy should throw on alert", "[wait_strategy]")
{
    disruptor::BlockingWaitStrategy strategy;
    disruptor::Sequence cursor(disruptor::Sequence::INITIAL_VALUE);
    std::atomic<bool> alerted{false};
    std::vector<disruptor::Sequence*> dependents;

    std::thread alerter([&alerted, &strategy] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        alerted.store(true, std::memory_order_release);
        strategy.signalAllWhenBlocking();
    });

    REQUIRE_THROWS_AS(strategy.waitFor(100, cursor, dependents, alerted), disruptor::AlertException);

    alerter.join();
}

TEST_CASE("BlockingWaitStrategy should wake up on signalAllWhenBlocking", "[wait_strategy]")
{
    disruptor::BlockingWaitStrategy strategy;
    disruptor::Sequence cursor(disruptor::Sequence::INITIAL_VALUE);
    std::atomic<bool> alerted{false};
    std::vector<disruptor::Sequence*> dependents;

    auto start = std::chrono::steady_clock::now();

    std::thread signaler([&cursor, &strategy] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        cursor.set(0);
        strategy.signalAllWhenBlocking();
    });

    strategy.waitFor(0, cursor, dependents, alerted);

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // 由于有 50us 超时，应该在 20ms 左右返回
    REQUIRE(duration.count() >= 15);
    REQUIRE(duration.count() < 100);

    signaler.join();
}

// ========== WaitStrategy with dependents ==========
TEST_CASE("WaitStrategy should consider dependent sequences", "[wait_strategy]")
{
    disruptor::YieldingWaitStrategy strategy;
    disruptor::Sequence cursor(10);
    std::atomic<bool> alerted{false};

    disruptor::Sequence dep1(5);
    disruptor::Sequence dep2(3);  // 最小值
    std::vector<disruptor::Sequence*> dependents{&dep1, &dep2};

    // 应该返回不超过最小依赖序列的值
    long result = strategy.waitFor(3, cursor, dependents, alerted);
    REQUIRE(result == 3);
}

TEST_CASE("WaitStrategy should wait for slowest dependent", "[wait_strategy]")
{
    disruptor::BusySpinWaitStrategy strategy;
    disruptor::Sequence cursor(100);
    std::atomic<bool> alerted{false};

    disruptor::Sequence dep1(disruptor::Sequence::INITIAL_VALUE);
    disruptor::Sequence dep2(50);
    std::vector<disruptor::Sequence*> dependents{&dep1, &dep2};

    std::thread advancer([&dep1] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        dep1.set(10);
    });

    long result = strategy.waitFor(5, cursor, dependents, alerted);
    REQUIRE(result >= 5);

    advancer.join();
}

// ========== TimeoutBlockingWaitStrategyTest (using BlockingWaitStrategy) ==========
TEST_CASE("BlockingWaitStrategy should handle timeout-like behavior", "[wait_strategy]")
{
    disruptor::BlockingWaitStrategy strategy;
    disruptor::Sequence cursor(disruptor::Sequence::INITIAL_VALUE);
    std::atomic<bool> alerted{false};
    std::vector<disruptor::Sequence*> dependents;

    auto start = std::chrono::steady_clock::now();

    // 设置警报来终止等待
    std::thread timeout([&alerted, &strategy] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        alerted.store(true, std::memory_order_release);
        strategy.signalAllWhenBlocking();
    });

    REQUIRE_THROWS_AS(strategy.waitFor(1000, cursor, dependents, alerted), disruptor::AlertException);

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // 应该在 100ms 左右超时
    REQUIRE(duration.count() >= 90);
    REQUIRE(duration.count() < 200);

    timeout.join();
}
