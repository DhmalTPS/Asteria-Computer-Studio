#pragma once
// Fixed-width integer names used throughout Asteria.
//
// The simulated machine is defined in exact bit widths (ASTERIA-32 spec section 1), so the
// code says u32 / i32 instead of unsigned / int, whose widths C++ does not fix.
//
// Convention: architectural values (register contents, addresses, instruction words) are
// stored as u32 and reinterpreted as signed only at the point where an operation is signed
// (SLT, SRA, DIV...). Unsigned arithmetic wraps modulo 2^32 exactly as the ISA requires
// (spec section 4.1); signed overflow in C++ is undefined behaviour and must never be relied on.

#include <cstdint>

namespace asteria {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

}  // namespace asteria
