#include <catch2/catch_test_macros.hpp>

#include <random>

#include "cpu/core/alu.hpp"

using namespace asteria;
using namespace asteria::cpu::core;
using isa::Op;

namespace {
constexpr u32 neg(i64 v) { return static_cast<u32>(v); }
}  // namespace

TEST_CASE("alu: add, sub and the logic operations wrap and combine bits") {
    CHECK(alu(AluOp::Add, 0xFFFF'FFFF, 1) == 0u);
    CHECK(alu(AluOp::Sub, 0, 1) == 0xFFFF'FFFFu);
    CHECK(alu(AluOp::And, 0xF0F0, 0xFF00) == 0xF000u);
    CHECK(alu(AluOp::Or, 0xF0F0, 0xFF00) == 0xFFF0u);
    CHECK(alu(AluOp::Xor, 0xF0F0, 0xFF00) == 0x0FF0u);
    CHECK(alu(AluOp::Nor, 0, 0) == 0xFFFF'FFFFu);
}

TEST_CASE("alu: shifts use only the low five bits of the amount (spec section 4.1)") {
    CHECK(alu(AluOp::Sll, 1, 31) == 0x8000'0000u);
    CHECK(alu(AluOp::Sll, 1, 32) == 1u);  // 32 & 31 = 0
    CHECK(alu(AluOp::Srl, 0x8000'0000, 31) == 1u);
    CHECK(alu(AluOp::Sra, 0x8000'0000, 31) == 0xFFFF'FFFFu);
    CHECK(alu(AluOp::Sra, 0x4000'0000, 30) == 1u);
    CHECK(alu(AluOp::Srl, 0xFFFF'FFFF, 0x21) == 0x7FFF'FFFFu);
}

TEST_CASE("alu: set-less-than compares signed or unsigned") {
    CHECK(alu(AluOp::Slt, neg(-1), 0) == 1u);
    CHECK(alu(AluOp::Sltu, neg(-1), 0) == 0u);
    CHECK(alu(AluOp::Slt, 3, 3) == 0u);
    CHECK(alu(AluOp::Sltu, 2, 3) == 1u);
}

TEST_CASE("alu: multiply-high matches 64-bit products") {
    std::mt19937 rng(42);
    for (int i = 0; i < 100'000; ++i) {
        const u32 a = rng(), b = rng();
        const i64 signed_product = i64{static_cast<i32>(a)} * i64{static_cast<i32>(b)};
        const u64 unsigned_product = u64{a} * u64{b};
        REQUIRE(alu(AluOp::Mul, a, b) == static_cast<u32>(unsigned_product));
        REQUIRE(alu(AluOp::Mulh, a, b) == static_cast<u32>(static_cast<u64>(signed_product) >> 32));
        REQUIRE(alu(AluOp::Mulhu, a, b) == static_cast<u32>(unsigned_product >> 32));
    }
    CHECK(alu(AluOp::Mulh, neg(-1), neg(-1)) == 0u);
    CHECK(alu(AluOp::Mulh, 0x8000'0000, 0x8000'0000) == 0x4000'0000u);
    CHECK(alu(AluOp::Mulhu, 0xFFFF'FFFF, 0xFFFF'FFFF) == 0xFFFF'FFFEu);
}

TEST_CASE("alu: division special cases never trap (spec section 4.2)") {
    for (const u32 dividend : {0u, 1u, 7u, 0x8000'0000u, 0xFFFF'FFFFu}) {
        CHECK(alu(AluOp::Div, dividend, 0) == 0xFFFF'FFFFu);
        CHECK(alu(AluOp::Divu, dividend, 0) == 0xFFFF'FFFFu);
        CHECK(alu(AluOp::Rem, dividend, 0) == dividend);
        CHECK(alu(AluOp::Remu, dividend, 0) == dividend);
    }
    CHECK(alu(AluOp::Div, 0x8000'0000, neg(-1)) == 0x8000'0000u);
    CHECK(alu(AluOp::Rem, 0x8000'0000, neg(-1)) == 0u);
    CHECK(alu(AluOp::Divu, 0x8000'0000, neg(-1)) == 0u);
    CHECK(alu(AluOp::Remu, 0x8000'0000, neg(-1)) == 0x8000'0000u);
}

TEST_CASE("alu: signed division rounds toward zero; the remainder takes the dividend's sign") {
    CHECK(alu(AluOp::Div, neg(-7), 2) == neg(-3));
    CHECK(alu(AluOp::Rem, neg(-7), 2) == neg(-1));
    CHECK(alu(AluOp::Div, 7, neg(-2)) == neg(-3));
    CHECK(alu(AluOp::Rem, 7, neg(-2)) == 1u);
    CHECK(alu(AluOp::Divu, neg(-7), 2) == 0x7FFF'FFFCu);
}

TEST_CASE("alu_op_of maps every immediate instruction to its register twin") {
    CHECK(alu_op_of(Op::Addi) == AluOp::Add);
    CHECK(alu_op_of(Op::Slti) == AluOp::Slt);
    CHECK(alu_op_of(Op::Sltiu) == AluOp::Sltu);
    CHECK(alu_op_of(Op::Andi) == AluOp::And);
    CHECK(alu_op_of(Op::Ori) == AluOp::Or);
    CHECK(alu_op_of(Op::Xori) == AluOp::Xor);
    CHECK(alu_op_of(Op::Slli) == AluOp::Sll);
    CHECK(alu_op_of(Op::Srli) == AluOp::Srl);
    CHECK(alu_op_of(Op::Srai) == AluOp::Sra);
    CHECK_FALSE(alu_op_of(Op::Lui).has_value());
    CHECK_FALSE(alu_op_of(Op::Lw).has_value());
    CHECK_FALSE(alu_op_of(Op::Beq).has_value());
}

TEST_CASE("branch_taken compares signed and unsigned as each branch requires") {
    CHECK(branch_taken(Op::Beq, 5, 5) == true);
    CHECK(branch_taken(Op::Bne, 5, 5) == false);
    CHECK(branch_taken(Op::Blt, neg(-1), 0) == true);
    CHECK(branch_taken(Op::Bltu, neg(-1), 0) == false);
    CHECK(branch_taken(Op::Bge, 0, neg(-1)) == true);
    CHECK(branch_taken(Op::Bgeu, 0, neg(-1)) == false);
    CHECK(branch_taken(Op::Bge, 4, 4) == true);
    CHECK_FALSE(branch_taken(Op::Add, 1, 1).has_value());
}
