#include <catch2/catch_test_macros.hpp>

#include "common/bits.hpp"

using namespace asteria;
using namespace asteria::bits;

// The functions are constexpr, so the most important cases are proven at compile time:
// if any of these is wrong, the test file does not even build.
static_assert(mask(0) == 0);
static_assert(mask(5) == 0b11111);
static_assert(mask(32) == 0xFFFF'FFFF);
static_assert(extract(0x000A'0044, 5, 0) == 0x04);    // ADDI R1, R0, 10 -> opcode
static_assert(extract(0x000A'0044, 10, 6) == 1);      //                -> rd
static_assert(extract(0x000A'0044, 31, 16) == 10);    //                -> imm16
static_assert(sign_extend(0xFFFF, 16) == 0xFFFF'FFFF);

TEST_CASE("extract reads inclusive bit ranges") {
    CHECK(extract(0xDEAD'BEEF, 31, 0) == 0xDEAD'BEEF);
    CHECK(extract(0xDEAD'BEEF, 31, 16) == 0xDEAD);
    CHECK(extract(0xDEAD'BEEF, 15, 0) == 0xBEEF);
    CHECK(extract(0xDEAD'BEEF, 3, 0) == 0xF);
    CHECK(extract(0x8000'0000, 31, 31) == 1);
    CHECK(extract(0x8000'0000, 30, 30) == 0);
}

TEST_CASE("insert is the inverse of extract") {
    // Encode ADD R3, R1, R2 field by field and compare with the spec's example (section 13).
    u32 word = 0;
    word = insert(word, 5, 0, 0x01);     // opcode OP
    word = insert(word, 10, 6, 3);       // rd
    word = insert(word, 15, 11, 1);      // rs1
    word = insert(word, 20, 16, 2);      // rs2
    word = insert(word, 31, 21, 0);      // funct11 ADD
    CHECK(word == 0x0002'08C1);

    SECTION("bits outside the field are preserved") {
        CHECK(insert(0xFFFF'FFFF, 15, 8, 0x00) == 0xFFFF'00FF);
    }
    SECTION("field bits beyond the width are discarded") {
        CHECK(insert(0, 3, 0, 0xFF) == 0x0F);
    }
    SECTION("round trip over every field position") {
        for (unsigned lo = 0; lo < 32; ++lo) {
            for (unsigned hi = lo; hi < 32; ++hi) {
                const u32 field = 0xA5A5'A5A5 & mask(hi - lo + 1);
                CHECK(extract(insert(0x1234'5678, hi, lo, field), hi, lo) == field);
            }
        }
    }
}

TEST_CASE("sign_extend widens two's-complement fields") {
    CHECK(sign_extend(0x7FFF, 16) == 0x0000'7FFF);  // largest positive 16-bit
    CHECK(sign_extend(0x8000, 16) == 0xFFFF'8000);  // most negative 16-bit
    CHECK(sign_extend(0x0000, 16) == 0);
    CHECK(sign_extend(0x1, 1) == 0xFFFF'FFFF);      // a 1-bit field "1" is -1
    CHECK(sign_extend(0x0, 1) == 0);
    CHECK(sign_extend(0xDEAD'BEEF, 32) == 0xDEAD'BEEF);  // full width is the identity
    CHECK(sign_extend(0xFFFF'0005, 16) == 5);       // garbage above the field is ignored

    SECTION("matches the arithmetic definition for every 8-bit value") {
        for (u32 v = 0; v < 256; ++v) {
            const i32 expected = v < 128 ? static_cast<i32>(v) : static_cast<i32>(v) - 256;
            CHECK(static_cast<i32>(sign_extend(v, 8)) == expected);
        }
    }
}

TEST_CASE("zero_extend keeps only the field") {
    CHECK(zero_extend(0xFFFF'FFFF, 16) == 0x0000'FFFF);
    CHECK(zero_extend(0x8000, 16) == 0x8000);
    CHECK(zero_extend(0xDEAD'BEEF, 32) == 0xDEAD'BEEF);
}

TEST_CASE("fits_signed and fits_unsigned bound immediate fields") {
    CHECK(fits_signed(32767, 16));
    CHECK(fits_signed(-32768, 16));
    CHECK_FALSE(fits_signed(32768, 16));
    CHECK_FALSE(fits_signed(-32769, 16));

    CHECK(fits_unsigned(0, 16));
    CHECK(fits_unsigned(65535, 16));
    CHECK_FALSE(fits_unsigned(65536, 16));
    CHECK_FALSE(fits_unsigned(-1, 16));
}

TEST_CASE("is_aligned checks power-of-two alignment") {
    CHECK(is_aligned(0x8000'0000, 4));
    CHECK(is_aligned(0x8000'0002, 2));
    CHECK_FALSE(is_aligned(0x8000'0002, 4));
    CHECK_FALSE(is_aligned(0x8000'0001, 2));
    CHECK(is_aligned(0x8000'0001, 1));  // byte accesses are always aligned
}
