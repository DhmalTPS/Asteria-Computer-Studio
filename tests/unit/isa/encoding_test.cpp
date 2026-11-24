#include <catch2/catch_test_macros.hpp>

#include <random>
#include <vector>

#include "common/bits.hpp"
#include "isa/encoding.hpp"

using namespace asteria;
using namespace asteria::isa;

namespace {

// Builds a valid Instruction for `op` from raw random bits, respecting the op's form.
Instruction random_instruction(Op op, std::mt19937& rng) {
    const Form form = info(op).form;
    const FieldUse used = fields_used(form);
    const ImmRange range = imm_range(imm_kind_of(form));
    Instruction in{op};
    if (used.rd) in.rd = static_cast<u8>(rng() % 32);
    if (used.rs1) in.rs1 = static_cast<u8>(rng() % 32);
    if (used.rs2) in.rs2 = static_cast<u8>(rng() % 32);
    const auto span = static_cast<u32>(static_cast<i64>(range.max) - range.min + 1);
    in.imm = static_cast<i32>(static_cast<i64>(range.min) + rng() % span);
    return in;
}

}  // namespace

TEST_CASE("encode reproduces the spec's examples (spec section 13)") {
    CHECK(encode({Op::Addi, 1, 0, 0, 10}) == 0x000A'0044u);
    CHECK(encode({Op::Add, 3, 1, 2, 0}) == 0x0002'08C1u);
    CHECK(encode({Op::Beq, 0, 1, 2, 3}) == 0x0002'08E0u);
    CHECK(encode({Op::Addi, 0, 0, 0, 0}) == kNop);
}

TEST_CASE("encode places each format's fields where spec section 2 says") {
    // SW R2, 0(R3): opcode 0x1A, rs1 = 3, rs2 = 2
    CHECK(encode({Op::Sw, 0, 3, 2, 0}) == 0x0002'181Au);
    // BNE R1, R0, -2: imm = 0xFFFE split into imm[15:5] = 0x7FF and imm[4:0] = 0x1E
    CHECK(encode({Op::Bne, 0, 1, 0, -2}) == 0xFFE0'0FA1u);
    // LUI R3, 0x8001
    CHECK(encode({Op::Lui, 3, 0, 0, 0x8001}) == 0x8001'00CDu);
    // HALT: SYSTEM group, funct11 = 4
    CHECK(encode({Op::Halt}) == 0x0080'0030u);
    // JAL R31, -1: imm21 = all ones (bits 31:11), rd = 31 (bits 10:6), opcode 0x28
    CHECK(encode({Op::Jal, 31, 0, 0, -1}) == 0xFFFF'FFE8u);
    // CSRRWI R1, STATUS, 5: the 5-bit operand rides in the rs1 field
    CHECK(encode({Op::Csrrwi, 1, 5, 0, 0x000}) == 0x0000'2874u);
}

TEST_CASE("decode inverts encode for every instruction") {
    std::mt19937 rng{2026};
    for (const OpInfo& row : kInstructionSet) {
        INFO(row.mnemonic);
        for (int trial = 0; trial < 2000; ++trial) {
            const Instruction in = random_instruction(row.op, rng);
            const auto word = encode(in);
            REQUIRE(word.has_value());
            REQUIRE(decode(*word) == in);
        }
        // The extremes of the immediate range must survive too.
        const ImmRange range = imm_range(imm_kind_of(row.form));
        for (const i32 imm : {range.min, range.max, i32{0}}) {
            Instruction in{row.op};
            in.imm = imm;
            REQUIRE(decode(encode(in).value()) == in);
        }
    }
}

TEST_CASE("encode inverts decode for every legal 32-bit word") {
    // Two million random words cover every opcode and many field combinations. A legal word
    // must come back bit-for-bit; that proves decoding loses no information.
    std::mt19937 rng{1977};
    unsigned legal = 0;
    unsigned mismatches = 0;
    u32 first_bad = 0;
    for (int i = 0; i < 2'000'000; ++i) {
        // Force a valid opcode half of the time, so legal words are common.
        u32 word = rng();
        if ((i & 1) != 0) {
            word = bits::insert(word, 5, 0, kInstructionSet[rng() % kOpCount].opcode);
        }
        if (const auto in = decode(word)) {
            ++legal;
            const auto again = encode(*in);
            if (!again || *again != word) {
                if (mismatches++ == 0) first_bad = word;
            }
        }
    }
    INFO("first mismatching word: " << first_bad);
    CHECK(mismatches == 0);
    CHECK(legal > 10'000);  // the test really exercised legal words
}

TEST_CASE("decoding is strict: unused fields must be zero (D3)") {
    SECTION("the all-zero word and reserved opcodes are illegal") {
        CHECK_FALSE(decode(0x0000'0000).has_value());
        for (const u32 opcode : {0x03u, 0x0Fu, 0x13u, 0x16u, 0x17u, 0x1Bu, 0x1Du, 0x1Eu, 0x1Fu,
                                 0x26u, 0x27u, 0x2Au, 0x2Fu, 0x37u, 0x3Fu}) {
            CHECK_FALSE(decode(0x1234'5600 | opcode).has_value());
        }
    }
    SECTION("an undefined funct11 inside a group is illegal") {
        CHECK_FALSE(decode(bits::insert(0x0000'0001, 31, 21, 11)).has_value());  // OP funct 11
        CHECK_FALSE(decode(bits::insert(0x0000'0002, 31, 21, 7)).has_value());   // MULDIV 7
        CHECK_FALSE(decode(bits::insert(0x0000'001C, 31, 21, 1)).has_value());   // AMO 1
        CHECK_FALSE(decode(bits::insert(0x0000'0030, 31, 21, 7)).has_value());   // SYSTEM 7
    }
    SECTION("flipping a bit in an existing but unused register field makes the word illegal") {
        std::mt19937 rng{7};
        for (const OpInfo& row : kInstructionSet) {
            INFO(row.mnemonic);
            const FieldUse present = fields_present(format_of(row.form));
            const FieldUse used = fields_used(row.form);
            const u32 word = encode(random_instruction(row.op, rng)).value();
            if (present.rd && !used.rd) CHECK_FALSE(decode(word | (1u << 6)).has_value());
            if (present.rs1 && !used.rs1) CHECK_FALSE(decode(word | (1u << 11)).has_value());
            if (present.rs2 && !used.rs2) CHECK_FALSE(decode(word | (1u << 16)).has_value());
        }
    }
    SECTION("shift amounts above 31 and CSR numbers above 4095 are illegal") {
        const u32 slli = encode({Op::Slli, 1, 2, 0, 31}).value();
        CHECK_FALSE(decode(slli | (1u << 21)).has_value());  // imm[5] set
        const u32 csrrw = encode({Op::Csrrw, 1, 2, 0, 0}).value();
        CHECK_FALSE(decode(csrrw | (1u << 28)).has_value());  // imm[12] set
    }
}

TEST_CASE("encode rejects values that do not fit, naming the field") {
    auto message = [](const Instruction& in) { return encode(in).error().message; };
    CHECK(message({Op::Add, 32, 1, 2, 0}) == "ADD: rd must be 0..31 (got 32)");
    CHECK(message({Op::Addi, 1, 0, 0, 32768}) == "ADDI: immediate 32768 out of range -32768..32767");
    CHECK(message({Op::Ori, 1, 0, 0, -1}) == "ORI: immediate -1 out of range 0..65535");
    CHECK(message({Op::Slli, 1, 1, 0, 32}) == "SLLI: immediate 32 out of range 0..31");
    CHECK(message({Op::Sw, 5, 1, 2, 0}) == "SW has no rd operand (field must be 0, got 5)");
    CHECK(message({Op::Ecall, 0, 0, 0, 1}) == "ECALL has no immediate (got 1)");
    CHECK(message({Op::Csrrwi, 1, 32, 0, 0}) == "CSRRWI: uimm5 must be 0..31 (got 32)");
    CHECK(message({Op::Jal, 31, 0, 0, 1 << 20}) ==
          "JAL: immediate 1048576 out of range -1048576..1048575");
}
