#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "common/result.hpp"

using namespace asteria;

namespace {

// A tiny example of the intended style: input validation returns Result.
Result<int> parse_register(std::string_view name) {
    if (name.size() < 2 || name[0] != 'R') {
        return fail("'{}' is not a register", name);
    }
    int number = 0;
    for (char c : name.substr(1)) {
        if (c < '0' || c > '9') {
            return fail("'{}' is not a register", name);
        }
        number = number * 10 + (c - '0');
    }
    if (number > 31) {
        return fail("register R{} does not exist (R0-R31)", number);
    }
    return number;
}

}  // namespace

TEST_CASE("Result carries either a value or a formatted error") {
    const auto ok = parse_register("R7");
    REQUIRE(ok.has_value());
    CHECK(*ok == 7);

    const auto bad = parse_register("R40");
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message == "register R40 does not exist (R0-R31)");

    CHECK_FALSE(parse_register("X1").has_value());
}
