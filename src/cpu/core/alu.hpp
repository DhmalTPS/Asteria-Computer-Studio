#pragma once
// The ALU and the branch comparator (spec §4.1-§4.3, §4.5), written once for every CPU (D23).
//
// An immediate instruction uses the same ALU operation as its register twin (ADDI = ADD with
// the immediate as the second operand), so the ALU knows 18 operations, not 27 instructions.
// alu_op_of() is that mapping. The pipeline (M5) will carry an AluOp in its control word.
//
// No operation traps (D4). Division by zero and INT_MIN / -1 have defined results (§4.2).
// Everything is constexpr, so the edge cases are also checked at compile time (bottom).

#include <optional>

#include "common/types.hpp"
#include "isa/instruction_set.hpp"

namespace asteria::cpu::core {

enum class AluOp : u8 {
    Add, Sub, And, Or, Xor, Nor, Sll, Srl, Sra, Slt, Sltu,
    Mul, Mulh, Mulhu, Div, Divu, Rem, Remu,
};

// The ALU operation an instruction uses, or nullopt if it isn't an ALU instruction
// (LUI and AUIPC are not: they build their result from the immediate and PC directly).
[[nodiscard]] constexpr std::optional<AluOp> alu_op_of(isa::Op op) noexcept {
    using isa::Op;
    switch (op) {
        case Op::Add: case Op::Addi: return AluOp::Add;
        case Op::Sub: return AluOp::Sub;
        case Op::And: case Op::Andi: return AluOp::And;
        case Op::Or: case Op::Ori: return AluOp::Or;
        case Op::Xor: case Op::Xori: return AluOp::Xor;
        case Op::Nor: return AluOp::Nor;
        case Op::Sll: case Op::Slli: return AluOp::Sll;
        case Op::Srl: case Op::Srli: return AluOp::Srl;
        case Op::Sra: case Op::Srai: return AluOp::Sra;
        case Op::Slt: case Op::Slti: return AluOp::Slt;
        case Op::Sltu: case Op::Sltiu: return AluOp::Sltu;
        case Op::Mul: return AluOp::Mul;
        case Op::Mulh: return AluOp::Mulh;
        case Op::Mulhu: return AluOp::Mulhu;
        case Op::Div: return AluOp::Div;
        case Op::Divu: return AluOp::Divu;
        case Op::Rem: return AluOp::Rem;
        case Op::Remu: return AluOp::Remu;
        default: return std::nullopt;
    }
}

// Values are u32; an operation reinterprets them as signed only where it is signed.
// (Converting a u32 to i32 is well defined since C++20: it wraps modulo 2^32.)
[[nodiscard]] constexpr u32 alu(AluOp op, u32 a, u32 b) noexcept {
    const auto sa = static_cast<i32>(a);
    const auto sb = static_cast<i32>(b);
    const unsigned shamt = b & 31u;  // shifts use only the low 5 bits (§4.1)

    switch (op) {
        case AluOp::Add: return a + b;  // u32 arithmetic wraps modulo 2^32, as required
        case AluOp::Sub: return a - b;
        case AluOp::And: return a & b;
        case AluOp::Or: return a | b;
        case AluOp::Xor: return a ^ b;
        case AluOp::Nor: return ~(a | b);
        case AluOp::Sll: return a << shamt;
        case AluOp::Srl: return a >> shamt;
        // >> on a negative signed value is an arithmetic shift since C++20.
        case AluOp::Sra: return static_cast<u32>(sa >> shamt);
        case AluOp::Slt: return sa < sb ? 1u : 0u;
        case AluOp::Sltu: return a < b ? 1u : 0u;

        case AluOp::Mul: return a * b;  // the low 32 bits are the same signed or unsigned
        case AluOp::Mulh: {
            // An i32 x i32 product always fits in i64, so this cannot overflow.
            const i64 product = i64{sa} * i64{sb};
            return static_cast<u32>(static_cast<u64>(product) >> 32);
        }
        case AluOp::Mulhu: return static_cast<u32>((u64{a} * u64{b}) >> 32);

        // §4.2 special cases. INT_MIN / -1 would overflow (undefined behaviour in C++), so
        // it is handled before the division.
        case AluOp::Div:
            if (b == 0) return 0xFFFF'FFFFu;
            if (a == 0x8000'0000u && b == 0xFFFF'FFFFu) return 0x8000'0000u;
            return static_cast<u32>(sa / sb);  // C++ division rounds toward zero, as required
        case AluOp::Divu:
            return b == 0 ? 0xFFFF'FFFFu : a / b;
        case AluOp::Rem:
            if (b == 0) return a;
            if (a == 0x8000'0000u && b == 0xFFFF'FFFFu) return 0;
            return static_cast<u32>(sa % sb);  // sign follows the dividend
        case AluOp::Remu:
            return b == 0 ? a : a % b;
    }
    return 0;
}

// Is a branch taken? nullopt if `op` is not a branch.
[[nodiscard]] constexpr std::optional<bool> branch_taken(isa::Op op, u32 a, u32 b) noexcept {
    using isa::Op;
    const auto sa = static_cast<i32>(a);
    const auto sb = static_cast<i32>(b);
    switch (op) {
        case Op::Beq: return a == b;
        case Op::Bne: return a != b;
        case Op::Blt: return sa < sb;
        case Op::Bge: return sa >= sb;
        case Op::Bltu: return a < b;
        case Op::Bgeu: return a >= b;
        default: return std::nullopt;
    }
}

// Spec §4.2's special cases, checked by the compiler.
static_assert(alu(AluOp::Div, 7, 0) == 0xFFFF'FFFFu);
static_assert(alu(AluOp::Rem, 7, 0) == 7);
static_assert(alu(AluOp::Div, 0x8000'0000u, 0xFFFF'FFFFu) == 0x8000'0000u);
static_assert(alu(AluOp::Rem, 0x8000'0000u, 0xFFFF'FFFFu) == 0);
static_assert(alu(AluOp::Div, static_cast<u32>(-7), 2) == static_cast<u32>(-3));  // toward zero
static_assert(alu(AluOp::Rem, static_cast<u32>(-7), 2) == static_cast<u32>(-1));

}  // namespace asteria::cpu::core
