#include <catch2/catch_test_macros.hpp>

#include "disruptor/sequence.h"

TEST_CASE("Sequence atomic operations")
{
    disruptor::Sequence seq(0);

    REQUIRE(seq.addAndGet(10) == 10);
    REQUIRE(seq.get() == 10);

    REQUIRE(seq.incrementAndGet() == 11);
    REQUIRE(seq.get() == 11);

    REQUIRE(seq.getAndAdd(5) == 11);
    REQUIRE(seq.get() == 16);

    REQUIRE(seq.compareAndSet(16, 20));
    REQUIRE(seq.get() == 20);
}
