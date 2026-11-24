#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string_view>

#include "isa/instruction_set.hpp"

using namespace asteria;
using namespace asteria::isa;

// An independent copy of spec §3, typed from the document rather than from the code. If the
// table in instruction_set.hpp drifts from the spec, this test catches it.
namespace {
struct SpecRow {
    std::string_view mnemonic;
    unsigned opcode;
    unsigned funct;
    Format format;
};
constexpr SpecRow kSpec[] = {
    {"ADD", 0x01, 0, Format::R},    {"SUB", 0x01, 1, Format::R},     {"AND", 0x01, 2, Format::R},
    {"OR", 0x01, 3, Format::R},     {"XOR", 0x01, 4, Format::R},     {"NOR", 0x01, 5, Format::R},
    {"SLL", 0x01, 6, Format::R},    {"SRL", 0x01, 7, Format::R},     {"SRA", 0x01, 8, Format::R},
    {"SLT", 0x01, 9, Format::R},    {"SLTU", 0x01, 10, Format::R},   {"MUL", 0x02, 0, Format::R},
    {"MULH", 0x02, 1, Format::R},   {"MULHU", 0x02, 2, Format::R},   {"DIV", 0x02, 3, Format::R},
    {"DIVU", 0x02, 4, Format::R},   {"REM", 0x02, 5, Format::R},     {"REMU", 0x02, 6, Format::R},
    {"ADDI", 0x04, 0, Format::I},   {"SLTI", 0x05, 0, Format::I},    {"SLTIU", 0x06, 0, Format::I},
    {"ANDI", 0x07, 0, Format::I},   {"ORI", 0x08, 0, Format::I},     {"XORI", 0x09, 0, Format::I},
    {"SLLI", 0x0A, 0, Format::I},   {"SRLI", 0x0B, 0, Format::I},    {"SRAI", 0x0C, 0, Format::I},
    {"LUI", 0x0D, 0, Format::I},    {"AUIPC", 0x0E, 0, Format::I},   {"LB", 0x10, 0, Format::I},
    {"LH", 0x11, 0, Format::I},     {"LW", 0x12, 0, Format::I},      {"LBU", 0x14, 0, Format::I},
    {"LHU", 0x15, 0, Format::I},    {"SB", 0x18, 0, Format::S},      {"SH", 0x19, 0, Format::S},
    {"SW", 0x1A, 0, Format::S},     {"SWAP", 0x1C, 0, Format::R},    {"BEQ", 0x20, 0, Format::B},
    {"BNE", 0x21, 0, Format::B},    {"BLT", 0x22, 0, Format::B},     {"BGE", 0x23, 0, Format::B},
    {"BLTU", 0x24, 0, Format::B},   {"BGEU", 0x25, 0, Format::B},    {"JAL", 0x28, 0, Format::J},
    {"JALR", 0x29, 0, Format::I},   {"ECALL", 0x30, 0, Format::R},   {"EBREAK", 0x30, 1, Format::R},
    {"ERET", 0x30, 2, Format::R},   {"WFI", 0x30, 3, Format::R},     {"HALT", 0x30, 4, Format::R},
    {"TLBFLUSH", 0x30, 5, Format::R}, {"FENCE.I", 0x30, 6, Format::R}, {"CSRRW", 0x31, 0, Format::I},
    {"CSRRS", 0x32, 0, Format::I},  {"CSRRC", 0x33, 0, Format::I},   {"CSRRWI", 0x34, 0, Format::I},
    {"CSRRSI", 0x35, 0, Format::I}, {"CSRRCI", 0x36, 0, Format::I},
};
}  // namespace

TEST_CASE("the instruction table matches the spec's opcode map row for row") {
    REQUIRE(std::size(kSpec) == kOpCount);
    for (const SpecRow& spec : kSpec) {
        INFO(spec.mnemonic);
        const auto op = find_op(spec.mnemonic);
        REQUIRE(op.has_value());
        const OpInfo& row = info(*op);
        CHECK(row.opcode == spec.opcode);
        CHECK(row.funct == spec.funct);
        CHECK(format_of(row.form) == spec.format);
    }
}

TEST_CASE("exactly the spec's 37 opcodes are assigned; the other 27 are illegal") {
    std::set<unsigned> used;
    for (const SpecRow& spec : kSpec) {
        used.insert(spec.opcode);
    }
    REQUIRE(used.size() == 37);
    for (unsigned opcode = 0; opcode < kOpcodeCount; ++opcode) {
        INFO("opcode " << opcode);
        CHECK((kByOpcode[opcode].count > 0) == used.contains(opcode));
    }
}

TEST_CASE("only ERET, WFI, HALT and TLBFLUSH are kernel-only instructions (spec section 6)") {
    for (const OpInfo& row : kInstructionSet) {
        INFO(row.mnemonic);
        const bool expected = row.op == Op::Eret || row.op == Op::Wfi || row.op == Op::Halt ||
                              row.op == Op::Tlbflush;
        CHECK(row.kernel_only == expected);
    }
}

TEST_CASE("find_op ignores case and rejects unknown mnemonics") {
    CHECK(find_op("addi") == Op::Addi);
    CHECK(find_op("AdDi") == Op::Addi);
    CHECK(find_op("fence.i") == Op::FenceI);
    CHECK_FALSE(find_op("ADDIU").has_value());
    CHECK_FALSE(find_op("").has_value());
    CHECK_FALSE(find_op("NOP").has_value());  // a pseudo-instruction, not in the table
}

TEST_CASE("forms map to the formats and immediates of spec section 2") {
    CHECK(format_of(Op::Swap) == Format::R);    // AMO group is R-type
    CHECK(format_of(Op::Jalr) == Format::I);    // a jump, but its operands fit I-type
    CHECK(format_of(Op::Csrrwi) == Format::I);
    CHECK(imm_kind_of(info(Op::Andi).form) == ImmKind::Unsigned16);  // logical: zext
    CHECK(imm_kind_of(info(Op::Sltiu).form) == ImmKind::Signed16);   // sext, compared unsigned
    CHECK(imm_kind_of(info(Op::Sw).form) == ImmKind::SplitSigned16);
    CHECK(imm_kind_of(info(Op::Jal).form) == ImmKind::Signed21);
}
