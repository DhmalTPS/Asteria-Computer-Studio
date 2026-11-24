#include <catch2/catch_test_macros.hpp>

#include <random>

#include "toolchain/axf/axf.hpp"

using namespace asteria;
using namespace asteria::toolchain::axf;

namespace {

Executable sample() {
    Executable exe;
    exe.entry = 0x8000'0004;
    exe.segments.push_back({0x8000'0000, 8, flag::kRead | flag::kExecute, {4, 0, 0, 0, 0x30, 0, 0x80, 0}});
    exe.segments.push_back({0x8000'1000, 3, flag::kRead, {'h', 'i', 0}});
    exe.segments.push_back({0x8000'2000, 0x2000, flag::kRead | flag::kWrite, {}});
    exe.symbols = {{"main", 0x8000'0000, true}, {"greeting", 0x8000'1000, false},
                   {"buffer", 0x8000'2000, false}};
    sort_symbols(exe.symbols);
    return exe;
}

}  // namespace

TEST_CASE("AXF: write then read gives back the same executable and the same bytes") {
    const Executable exe = sample();
    const Result<std::vector<u8>> bytes = write(exe);
    REQUIRE(bytes.has_value());
    const Result<Executable> back = read(*bytes);
    REQUIRE(back.has_value());
    CHECK(*back == exe);
    CHECK(write(*back).value() == *bytes);

    // Header: magic, version 1.0, type EXEC, entry, counts (docs/formats/AXF.md).
    const std::vector<u8>& b = *bytes;
    CHECK(b[0] == 'A');
    CHECK(b[3] == 0);
    CHECK(b[4] == 1);
    CHECK(b[8] == 2);
    CHECK(b[12] == 0x04);
    CHECK(b[15] == 0x80);
    CHECK(b[16] == 3);  // segments
    CHECK(b[20] == 3);  // symbols
    CHECK(b.size() == 32 + 3 * 20 + 3 * 12 + (7 + 9 + 5) + 8 + 3);
}

TEST_CASE("AXF: validate states every semantic rule") {
    const auto error_of = [](const Executable& exe) {
        const Result<void> r = validate(exe);
        REQUIRE_FALSE(r.has_value());
        return r.error().message;
    };
    CHECK(validate(sample()).has_value());

    Executable e = sample();
    e.segments.clear();
    CHECK(error_of(e) == "executable has no segments");

    e = sample();
    e.segments[1].vaddr = 0x8000'1004;
    CHECK(error_of(e) == "segment 1: address 0x80001004 is not page-aligned");

    e = sample();
    e.segments[1].vaddr = 0x8000'0000;
    CHECK(error_of(e) == "segment 1: overlaps segment 0 or is out of address order");

    e = sample();
    e.segments[1].flags = flag::kWrite;
    CHECK(error_of(e) == "segment 1: flags 0x2 invalid (R must be set; only R, W, X exist)");

    e = sample();
    e.segments[1].mem_size = 2;
    CHECK(error_of(e) == "segment 1: 3 initialized bytes exceed its size of 2 bytes");

    e = sample();
    e.entry = 0x8000'1000;  // in a segment, but not an executable one
    CHECK(error_of(e) == "entry point 0x80001000 is not a word inside an executable segment's data");
    e.entry = 0x8000'0002;
    CHECK(error_of(e).starts_with("entry point 0x80000002"));

    e = sample();
    e.symbols.push_back({"main", 0x9000'0000, false});
    CHECK(error_of(e) == "symbol 'main' is defined twice");

    e = sample();
    std::swap(e.symbols[0], e.symbols[2]);
    CHECK(error_of(e).starts_with("symbols are not sorted"));
}

TEST_CASE("AXF: read refuses malformed files with a reason") {
    const std::vector<u8> good = write(sample()).value();
    const auto error_of = [](std::vector<u8> bytes) {
        const Result<Executable> r = read(bytes);
        REQUIRE_FALSE(r.has_value());
        return r.error().message;
    };

    CHECK(error_of({1, 2, 3}) == "not an AXF file: only 3 bytes");

    std::vector<u8> b = good;
    b[0] = 'E';
    CHECK(error_of(b) == "not an AXF file: bad magic number");

    b = good;
    b[4] = 2;
    CHECK(error_of(b) == "unsupported AXF version 2.0 (this build reads 1.0)");

    b = good;
    b[8] = 1;
    CHECK(error_of(b) == "this is a relocatable object (.aso); linking arrives in M9");

    b = good;
    b.push_back(0);
    CHECK(error_of(b) == "1 unexpected bytes at the end of the file");

    b = good;
    b.resize(b.size() - 1);
    CHECK(error_of(b).starts_with("segment 1: data runs past the end"));

    b = good;
    b[16] = 0xFF;  // segment count 255: tables larger than the file
    CHECK(error_of(b).starts_with("truncated file"));
}

TEST_CASE("AXF: read never crashes on corrupted input") {
    const std::vector<u8> good = write(sample()).value();
    std::mt19937 rng(17);
    int accepted = 0;
    for (int i = 0; i < 20'000; ++i) {
        std::vector<u8> b = good;
        const int flips = 1 + static_cast<int>(rng() % 4);
        for (int k = 0; k < flips; ++k) b[rng() % b.size()] ^= static_cast<u8>(1u << (rng() % 8));
        if (rng() % 8 == 0) b.resize(rng() % b.size());
        const Result<Executable> r = read(b);
        if (r) {
            ++accepted;
            // Anything accepted must be valid and canonical.
            REQUIRE(validate(*r).has_value());
            REQUIRE(write(*r).value() == b);
        }
    }
    SUCCEED(accepted << " corrupted files happened to be valid executables");
}
