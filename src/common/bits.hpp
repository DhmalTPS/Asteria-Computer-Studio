#pragma once
// Bit-field manipulation on 32-bit words.
//
// These operations are the vocabulary of the ISA spec. Decoding an instruction is a sequence
// of extract() calls, and computing an immediate is extract() followed by sign_extend().
// The names follow the spec's notation:
//
//     spec            code
//     x[hi:lo]        extract(x, hi, lo)
//     sext(v)         sign_extend(v, width)
//     zext(v)         zero_extend(v, width)
//
// Everything is constexpr, so the encoding tables in isa/ can be checked at compile time
// with static_assert.

#include "common/assert.hpp"
#include "common/types.hpp"

namespace asteria::bits {

// A mask of the low `width` bits: mask(5) == 0b11111. Valid for width in [0, 32].
// (1u << 32 is undefined behaviour in C++, hence the special case.)
[[nodiscard]] constexpr u32 mask(unsigned width) noexcept {
    ASTERIA_ASSERT(width <= 32, "mask width out of range");
    return width == 32 ? 0xFFFF'FFFFu : (1u << width) - 1u;
}

// x[hi:lo]: the bits hi..lo of x (inclusive), shifted down to bit 0.
// Example: extract(0x000A'0044, 5, 0) == 0x04 (the opcode of ADDI R1, R0, 10).
[[nodiscard]] constexpr u32 extract(u32 x, unsigned hi, unsigned lo) noexcept {
    ASTERIA_ASSERT(hi < 32 && lo <= hi, "invalid bit range");
    return (x >> lo) & mask(hi - lo + 1);
}

// Returns x with bits hi..lo replaced by the low bits of `field`. Inverse of extract().
// Used by the encoder: start from 0 and insert opcode, rd, rs1, ... one by one.
[[nodiscard]] constexpr u32 insert(u32 x, unsigned hi, unsigned lo, u32 field) noexcept {
    ASTERIA_ASSERT(hi < 32 && lo <= hi, "invalid bit range");
    const u32 field_mask = mask(hi - lo + 1) << lo;
    return (x & ~field_mask) | ((field << lo) & field_mask);
}

// zext: keeps the low `width` bits and clears the rest.
[[nodiscard]] constexpr u32 zero_extend(u32 value, unsigned width) noexcept {
    return value & mask(width);
}

// sext: treats the low `width` bits as a two's-complement number and widens it to 32 bits.
// Example: sign_extend(0xFFFF, 16) == 0xFFFF'FFFF (-1); sign_extend(0x7FFF, 16) == 0x7FFF.
//
// How it works (no branches): let m = the sign bit of the field. XOR with m flips the sign
// bit; subtracting m then borrows through every higher bit exactly when the sign bit was 1.
//   sign bit 0:  (v ^ m) - m = (v + m) - m = v
//   sign bit 1:  (v ^ m) - m = (v - m) - m = v - 2m = v - 2^width  -> the negative value
[[nodiscard]] constexpr u32 sign_extend(u32 value, unsigned width) noexcept {
    ASTERIA_ASSERT(width >= 1 && width <= 32, "sign_extend width out of range");
    const u32 sign_bit = 1u << (width - 1);
    return ((value & mask(width)) ^ sign_bit) - sign_bit;
}

// Does the signed value fit in a `width`-bit two's-complement field?
// The assembler uses this to reject immediates that would not survive the round trip
// through encoding (for example ADDI's 16-bit field holds -32768..32767).
[[nodiscard]] constexpr bool fits_signed(i64 value, unsigned width) noexcept {
    ASTERIA_ASSERT(width >= 1 && width <= 32, "width out of range");
    const i64 limit = i64{1} << (width - 1);
    return value >= -limit && value < limit;
}

// Does the non-negative value fit in a `width`-bit unsigned field? (ORI's zext immediate.)
[[nodiscard]] constexpr bool fits_unsigned(i64 value, unsigned width) noexcept {
    ASTERIA_ASSERT(width >= 1 && width <= 32, "width out of range");
    return value >= 0 && value < (i64{1} << width);
}

// Is `address` a multiple of `alignment`? `alignment` must be a power of two (1, 2, 4...).
// LW needs is_aligned(ea, 4), LH needs is_aligned(ea, 2) (spec section 4.4).
[[nodiscard]] constexpr bool is_aligned(u32 address, u32 alignment) noexcept {
    ASTERIA_ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0,
                   "alignment must be a power of two");
    return (address & (alignment - 1)) == 0;
}

}  // namespace asteria::bits
