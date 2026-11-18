#include <catch2/catch_test_macros.hpp>

#include "common/sim_time.hpp"

using namespace asteria;

static_assert(SimTime::ns(1) == SimTime::ps(1'000));
static_assert(SimTime::ps(1) == SimTime::fs(1'000));

TEST_CASE("unit constructors agree") {
    CHECK(SimTime::us(1).femtoseconds() == 1'000'000'000);
    CHECK(SimTime::ns(50).nanoseconds() == 50.0);
    CHECK(SimTime::ps(220) + SimTime::ps(580) == SimTime::ps(800));
    CHECK(SimTime::ns(1) - SimTime::ps(1) == SimTime::ps(999));
    CHECK(SimTime::ps(220) * 5 == SimTime::ps(1'100));
}

TEST_CASE("clock periods from frequencies") {
    CHECK(SimTime::period_of_hz(1'000'000'000) == SimTime::ns(1));  // 1 GHz
    CHECK(SimTime::period_of_hz(1'000'000) == SimTime::us(1));      // 1 MHz timer
    // DDR4-2400: 1.2 GHz command clock -> 833.333... ps, stored as 833'333 fs.
    CHECK(SimTime::period_of_hz(1'200'000'000).femtoseconds() == 833'333);
}

TEST_CASE("cycles_to_cover rounds up to whole cycles") {
    // The example from decision D8: one 50 ns DRAM access, three clock periods.
    CHECK(cycles_to_cover(SimTime::ns(50), SimTime::ps(800)) == 63);   // single-cycle CPU
    CHECK(cycles_to_cover(SimTime::ns(50), SimTime::ps(220)) == 228);  // pipelined CPU

    CHECK(cycles_to_cover(SimTime::ps(800), SimTime::ps(800)) == 1);   // exact multiple
    CHECK(cycles_to_cover(SimTime::ps(801), SimTime::ps(800)) == 2);   // just over
    CHECK(cycles_to_cover(SimTime{}, SimTime::ps(800)) == 0);          // nothing to wait for
}
