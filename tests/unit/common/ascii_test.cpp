#include <catch2/catch_test_macros.hpp>

#include "common/ascii.hpp"

using namespace asteria::ascii;

static_assert(iequals("addi", "ADDI"));
static_assert(!iequals("addi", "ADD"));
static_assert(to_upper('z') == 'Z' && to_upper('.') == '.');

TEST_CASE("iequals compares ASCII names without regard to case") {
    CHECK(iequals("Fence.I", "FENCE.I"));
    CHECK(iequals("", ""));
    CHECK_FALSE(iequals("sp", "s"));
    CHECK_FALSE(iequals("a[", "A{"));  // '[' and '{' differ by 0x20 but are not letters
}

TEST_CASE("to_upper converts only ASCII letters") {
    CHECK(to_upper("csrrw r1, status") == "CSRRW R1, STATUS");
    CHECK(to_lower('Q') == 'q');
    CHECK(is_digit('7'));
    CHECK_FALSE(is_alpha('_'));
}
