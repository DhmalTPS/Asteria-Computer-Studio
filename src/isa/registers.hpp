#pragma once
// Register names: R0-R31 and the ABI names of the calling convention (spec §12).
//
// The hardware only knows numbers 0..31. The names are a convention between programmers
// (and the compiler) about what each register is FOR: a0-a5 carry arguments, sp is the stack
// pointer, and so on. The assembler accepts both spellings, case-insensitively (§11.1).

#include <array>
#include <optional>
#include <string_view>

#include "common/types.hpp"

namespace asteria::isa {

inline constexpr unsigned kRegisterCount = 32;

// Well-known register numbers (spec §12).
namespace reg {
inline constexpr u8 kZero = 0;
inline constexpr u8 kAt = 1;
inline constexpr u8 kV0 = 2;   // return value; syscall number
inline constexpr u8 kA0 = 4;   // first argument; HALT's exit code
inline constexpr u8 kGp = 28;
inline constexpr u8 kSp = 29;
inline constexpr u8 kFp = 30;
inline constexpr u8 kRa = 31;  // return address (JAL/CALL)
}  // namespace reg

inline constexpr std::array<std::string_view, kRegisterCount> kNumericNames = {
    "R0",  "R1",  "R2",  "R3",  "R4",  "R5",  "R6",  "R7",  "R8",  "R9",  "R10",
    "R11", "R12", "R13", "R14", "R15", "R16", "R17", "R18", "R19", "R20", "R21",
    "R22", "R23", "R24", "R25", "R26", "R27", "R28", "R29", "R30", "R31"};

inline constexpr std::array<std::string_view, kRegisterCount> kAbiNames = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "a4", "a5", "t0",
    "t1",   "t2", "t3", "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3",
    "s4",   "s5", "s6", "s7", "t8", "t9", "gp", "sp", "fp", "ra"};

// "R5", "r5", "a1", "SP", "zero" and "s8" (an alias of fp) all parse. "R32", "R05" and "x5" don't.
[[nodiscard]] std::optional<u8> parse_register(std::string_view name) noexcept;

}  // namespace asteria::isa
