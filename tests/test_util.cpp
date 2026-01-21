#include <climits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "disruptor/sequence.h"
#include "disruptor/producer_sequencer.h"
#include "disruptor/util.h"

// UtilTest - 测试工具方法的正确性

// ========== getMinimumSequence Tests ==========
TEST_CASE("getMinimumSequence should return default value for empty sequences", "[util]")
{
    std::vector<disruptor::Sequence*> sequences;

    REQUIRE(disruptor::getMinimumSequence(sequences, 100) == 100);
    REQUIRE(disruptor::getMinimumSequence(sequences, -1) == -1);
    REQUIRE(disruptor::getMinimumSequence(sequences, 0) == 0);
}

TEST_CASE("getMinimumSequence should return single sequence value", "[util]")
{
    disruptor::Sequence seq(42);
    std::vector<disruptor::Sequence*> sequences{&seq};

    REQUIRE(disruptor::getMinimumSequence(sequences, 100) == 42);
}

TEST_CASE("getMinimumSequence should return minimum of multiple sequences", "[util]")
{
    disruptor::Sequence seq1(10);
    disruptor::Sequence seq2(5);
    disruptor::Sequence seq3(20);
    std::vector<disruptor::Sequence*> sequences{&seq1, &seq2, &seq3};

    REQUIRE(disruptor::getMinimumSequence(sequences, 100) == 5);
}

TEST_CASE("getMinimumSequence should handle negative sequences", "[util]")
{
    disruptor::Sequence seq1(-1);
    disruptor::Sequence seq2(-5);
    disruptor::Sequence seq3(0);
    std::vector<disruptor::Sequence*> sequences{&seq1, &seq2, &seq3};

    REQUIRE(disruptor::getMinimumSequence(sequences, 100) == -5);
}

TEST_CASE("getMinimumSequence should handle all equal sequences", "[util]")
{
    disruptor::Sequence seq1(42);
    disruptor::Sequence seq2(42);
    disruptor::Sequence seq3(42);
    std::vector<disruptor::Sequence*> sequences{&seq1, &seq2, &seq3};

    REQUIRE(disruptor::getMinimumSequence(sequences, 100) == 42);
}

TEST_CASE("getMinimumSequence should handle INITIAL_VALUE sequences", "[util]")
{
    disruptor::Sequence seq1(disruptor::Sequence::INITIAL_VALUE);
    disruptor::Sequence seq2(0);
    std::vector<disruptor::Sequence*> sequences{&seq1, &seq2};

    REQUIRE(disruptor::getMinimumSequence(sequences, 100) == disruptor::Sequence::INITIAL_VALUE);
}

// ========== isPowerOfTwo Tests ==========
TEST_CASE("isPowerOfTwo should identify powers of two correctly", "[util]")
{
    // 2^0 到 2^20
    REQUIRE(disruptor::isPowerOfTwo(1));
    REQUIRE(disruptor::isPowerOfTwo(2));
    REQUIRE(disruptor::isPowerOfTwo(4));
    REQUIRE(disruptor::isPowerOfTwo(8));
    REQUIRE(disruptor::isPowerOfTwo(16));
    REQUIRE(disruptor::isPowerOfTwo(32));
    REQUIRE(disruptor::isPowerOfTwo(64));
    REQUIRE(disruptor::isPowerOfTwo(128));
    REQUIRE(disruptor::isPowerOfTwo(256));
    REQUIRE(disruptor::isPowerOfTwo(512));
    REQUIRE(disruptor::isPowerOfTwo(1024));
    REQUIRE(disruptor::isPowerOfTwo(2048));
    REQUIRE(disruptor::isPowerOfTwo(4096));
    REQUIRE(disruptor::isPowerOfTwo(8192));
    REQUIRE(disruptor::isPowerOfTwo(16384));
    REQUIRE(disruptor::isPowerOfTwo(32768));
    REQUIRE(disruptor::isPowerOfTwo(65536));
    REQUIRE(disruptor::isPowerOfTwo(131072));
    REQUIRE(disruptor::isPowerOfTwo(262144));
    REQUIRE(disruptor::isPowerOfTwo(524288));
    REQUIRE(disruptor::isPowerOfTwo(1048576));
}

TEST_CASE("isPowerOfTwo should reject non-powers of two", "[util]")
{
    REQUIRE_FALSE(disruptor::isPowerOfTwo(0));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(-1));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(-2));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(3));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(5));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(6));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(7));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(9));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(10));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(12));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(15));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(17));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(100));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(1000));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(1023));
    REQUIRE_FALSE(disruptor::isPowerOfTwo(1025));
}

// ========== log2i Tests ==========
TEST_CASE("log2i should return correct logarithm for powers of two", "[util]")
{
    REQUIRE(disruptor::log2i(1) == 0);
    REQUIRE(disruptor::log2i(2) == 1);
    REQUIRE(disruptor::log2i(4) == 2);
    REQUIRE(disruptor::log2i(8) == 3);
    REQUIRE(disruptor::log2i(16) == 4);
    REQUIRE(disruptor::log2i(32) == 5);
    REQUIRE(disruptor::log2i(64) == 6);
    REQUIRE(disruptor::log2i(128) == 7);
    REQUIRE(disruptor::log2i(256) == 8);
    REQUIRE(disruptor::log2i(512) == 9);
    REQUIRE(disruptor::log2i(1024) == 10);
    REQUIRE(disruptor::log2i(2048) == 11);
    REQUIRE(disruptor::log2i(4096) == 12);
}

TEST_CASE("log2i should round up for non-powers of two", "[util]")
{
    // log2i 返回 ceil(log2(n))
    REQUIRE(disruptor::log2i(3) == 2);   // ceil(log2(3)) = 2
    REQUIRE(disruptor::log2i(5) == 3);   // ceil(log2(5)) = 3
    REQUIRE(disruptor::log2i(6) == 3);   // ceil(log2(6)) = 3
    REQUIRE(disruptor::log2i(7) == 3);   // ceil(log2(7)) = 3
    REQUIRE(disruptor::log2i(9) == 4);   // ceil(log2(9)) = 4
    REQUIRE(disruptor::log2i(15) == 4);  // ceil(log2(15)) = 4
    REQUIRE(disruptor::log2i(17) == 5);  // ceil(log2(17)) = 5
}

// ========== Sequence Constants Tests ==========
TEST_CASE("Sequence INITIAL_VALUE should be -1", "[util]")
{
    REQUIRE(disruptor::Sequence::INITIAL_VALUE == -1L);
}

TEST_CASE("Sequence default constructor should use INITIAL_VALUE", "[util]")
{
    disruptor::Sequence seq;
    REQUIRE(seq.get() == disruptor::Sequence::INITIAL_VALUE);
}

// ========== Sequence Operations Tests ==========
TEST_CASE("Sequence set and get should be consistent", "[util]")
{
    disruptor::Sequence seq;

    seq.set(42);
    REQUIRE(seq.get() == 42);

    seq.set(0);
    REQUIRE(seq.get() == 0);

    seq.set(-100);
    REQUIRE(seq.get() == -100);

    seq.set(LONG_MAX);
    REQUIRE(seq.get() == LONG_MAX);
}

TEST_CASE("Sequence setVolatile should work correctly", "[util]")
{
    disruptor::Sequence seq;

    seq.setVolatile(100);
    REQUIRE(seq.get() == 100);
}

TEST_CASE("Sequence compareAndSet should succeed when expected matches", "[util]")
{
    disruptor::Sequence seq(10);

    REQUIRE(seq.compareAndSet(10, 20));
    REQUIRE(seq.get() == 20);
}

TEST_CASE("Sequence compareAndSet should fail when expected does not match", "[util]")
{
    disruptor::Sequence seq(10);

    REQUIRE_FALSE(seq.compareAndSet(5, 20));
    REQUIRE(seq.get() == 10);  // 值不应该改变
}

TEST_CASE("Sequence incrementAndGet should increment by one", "[util]")
{
    disruptor::Sequence seq(0);

    REQUIRE(seq.incrementAndGet() == 1);
    REQUIRE(seq.incrementAndGet() == 2);
    REQUIRE(seq.incrementAndGet() == 3);
    REQUIRE(seq.get() == 3);
}

TEST_CASE("Sequence addAndGet should add and return new value", "[util]")
{
    disruptor::Sequence seq(0);

    REQUIRE(seq.addAndGet(5) == 5);
    REQUIRE(seq.addAndGet(10) == 15);
    REQUIRE(seq.addAndGet(-3) == 12);
    REQUIRE(seq.get() == 12);
}

TEST_CASE("Sequence getAndAdd should return old value and add", "[util]")
{
    disruptor::Sequence seq(0);

    REQUIRE(seq.getAndAdd(5) == 0);
    REQUIRE(seq.get() == 5);

    REQUIRE(seq.getAndAdd(10) == 5);
    REQUIRE(seq.get() == 15);
}

// ========== Exception Classes Tests ==========
TEST_CASE("AlertException should have correct message", "[util]")
{
    disruptor::AlertException ex;
    REQUIRE(std::string(ex.what()) == "Alerted");
}

TEST_CASE("InsufficientCapacityException should have correct message", "[util]")
{
    disruptor::InsufficientCapacityException ex;
    REQUIRE(std::string(ex.what()) == "Insufficient capacity");
}

// ========== Padding Tests ==========
TEST_CASE("Sequence should be cache-line aligned", "[util]")
{
    disruptor::Sequence seq;
    // Sequence 应该有 64 字节对齐
    REQUIRE(sizeof(disruptor::Sequence) >= 64);
}

TEST_CASE("Multiple Sequences should not share cache lines", "[util]")
{
    disruptor::Sequence seq1;
    disruptor::Sequence seq2;

    // 两个 Sequence 的地址差应该至少为 64 字节
    auto addr1 = reinterpret_cast<std::uintptr_t>(&seq1);
    auto addr2 = reinterpret_cast<std::uintptr_t>(&seq2);
    auto diff = (addr1 > addr2) ? (addr1 - addr2) : (addr2 - addr1);

    REQUIRE(diff >= 64);
}
