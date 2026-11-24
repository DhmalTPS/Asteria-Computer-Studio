#pragma once
// The ASTERIA-32 instruction table (spec §3): the single source of truth for every instruction.
//
// Each of the 59 instructions is ONE row below: its mnemonic, opcode, funct11 value, operand
// form and privilege. Everything else is derived from these rows, never typed twice:
//   - the decoder's opcode lookup table (kByOpcode, built at compile time below),
//   - which register fields an instruction uses, and how its immediate is decoded (Form),
//   - the encoder, the disassembler and the assembler's operand parser.
// Decision D23 requires this: LC-John's simulators re-implemented decoding three times and
// the three copies disagreed. Here, a mistake can only exist in one place, and the
// compile-time checks at the bottom of this file reject an inconsistent table outright.

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "common/types.hpp"

namespace asteria::isa {

// The bit layout of a 32-bit instruction word (spec §2). S and B share one layout; they are
// kept apart because their immediates mean different things (byte offset vs instruction count).
enum class Format : u8 { R, I, S, B, J };

// The operand form: which fields an instruction uses, how its immediate is interpreted, and
// how its operands are written in assembly. The format follows from the form.
enum class Form : u8 {
    RegRegReg,    // ADD   rd, rs1, rs2
    RegRegImm,    // ADDI  rd, rs1, imm       imm: signed 16-bit, sign-extended
    RegRegUimm,   // ORI   rd, rs1, uimm      uimm: unsigned 16-bit, zero-extended
    RegRegShamt,  // SLLI  rd, rs1, shamt     shamt: 0..31, imm[15:5] must be 0
    RegUimm,      // LUI   rd, uimm           rs1 field must be 0
    Load,         // LW    rd, imm(rs1)
    Store,        // SW    rs2, imm(rs1)      immediate split over [31:21] and [10:6]
    Branch,       // BEQ   rs1, rs2, target   imm counts instructions (bytes = imm x 4)
    Jal,          // JAL   rd, target         21-bit imm counts instructions
    Jalr,         // JALR  rd, rs1, imm
    Swap,         // SWAP  rd, rs2, (rs1)
    NoOperands,   // ECALL                    rd, rs1, rs2 fields must be 0
    TlbFlush,     // TLBFLUSH rs1, rs2        rd field must be 0
    CsrReg,       // CSRRW  rd, csr, rs1      imm[11:0] = CSR number, imm[15:12] must be 0
    CsrImm,       // CSRRWI rd, csr, uimm5    the 5-bit operand sits in the rs1 field
};

// How the immediate bits of a word become the immediate VALUE (spec §2 "Immediate decoding").
enum class ImmKind : u8 {
    None,           // no immediate
    Signed16,       // sext(inst[31:16])                      -32768..32767
    Unsigned16,     // zext(inst[31:16])                      0..65535
    Shamt5,         // inst[20:16], with inst[31:21] = 0      0..31
    Csr12,          // inst[27:16], with inst[31:28] = 0      0..4095
    SplitSigned16,  // sext(inst[31:21] : inst[10:6])         -32768..32767
    Signed21,       // sext(inst[31:11])                      -1048576..1048575
};

// Which of the three register fields an instruction uses. (For CsrImm, "rs1" is the 5-bit
// unsigned operand that occupies the rs1 field.)
struct FieldUse {
    bool rd = false;
    bool rs1 = false;
    bool rs2 = false;
};

[[nodiscard]] constexpr Format format_of(Form form) noexcept {
    switch (form) {
        case Form::RegRegReg:
        case Form::Swap:
        case Form::NoOperands:
        case Form::TlbFlush: return Format::R;
        case Form::Store: return Format::S;
        case Form::Branch: return Format::B;
        case Form::Jal: return Format::J;
        case Form::RegRegImm:
        case Form::RegRegUimm:
        case Form::RegRegShamt:
        case Form::RegUimm:
        case Form::Load:
        case Form::Jalr:
        case Form::CsrReg:
        case Form::CsrImm: return Format::I;
    }
    return Format::R;  // unreachable: every enumerator is handled above
}

[[nodiscard]] constexpr ImmKind imm_kind_of(Form form) noexcept {
    switch (form) {
        case Form::RegRegImm:
        case Form::Load:
        case Form::Jalr: return ImmKind::Signed16;
        case Form::RegRegUimm:
        case Form::RegUimm: return ImmKind::Unsigned16;
        case Form::RegRegShamt: return ImmKind::Shamt5;
        case Form::CsrReg:
        case Form::CsrImm: return ImmKind::Csr12;
        case Form::Store:
        case Form::Branch: return ImmKind::SplitSigned16;
        case Form::Jal: return ImmKind::Signed21;
        case Form::RegRegReg:
        case Form::Swap:
        case Form::NoOperands:
        case Form::TlbFlush: return ImmKind::None;
    }
    return ImmKind::None;  // unreachable
}

[[nodiscard]] constexpr FieldUse fields_used(Form form) noexcept {
    switch (form) {
        case Form::RegRegReg:
        case Form::Swap: return {.rd = true, .rs1 = true, .rs2 = true};
        case Form::RegRegImm:
        case Form::RegRegUimm:
        case Form::RegRegShamt:
        case Form::Load:
        case Form::Jalr:
        case Form::CsrReg:
        case Form::CsrImm: return {.rd = true, .rs1 = true, .rs2 = false};
        case Form::RegUimm:
        case Form::Jal: return {.rd = true, .rs1 = false, .rs2 = false};
        case Form::Store:
        case Form::Branch:
        case Form::TlbFlush: return {.rd = false, .rs1 = true, .rs2 = true};
        case Form::NoOperands: return {};
    }
    return {};  // unreachable
}

// Which register fields physically EXIST in a format's bit layout. A field that exists but
// that the instruction does not use must be zero (spec §2, D3). Fields that don't exist are
// immediate bits, so they are never checked as registers.
[[nodiscard]] constexpr FieldUse fields_present(Format format) noexcept {
    switch (format) {
        case Format::R: return {.rd = true, .rs1 = true, .rs2 = true};
        case Format::I: return {.rd = true, .rs1 = true, .rs2 = false};
        case Format::S:
        case Format::B: return {.rd = false, .rs1 = true, .rs2 = true};
        case Format::J: return {.rd = true, .rs1 = false, .rs2 = false};
    }
    return {};  // unreachable
}

// The valid range of an immediate VALUE for each kind (inclusive).
struct ImmRange {
    i32 min;
    i32 max;
};

[[nodiscard]] constexpr ImmRange imm_range(ImmKind kind) noexcept {
    switch (kind) {
        case ImmKind::None: return {0, 0};
        case ImmKind::Signed16:
        case ImmKind::SplitSigned16: return {-32768, 32767};
        case ImmKind::Unsigned16: return {0, 65535};
        case ImmKind::Shamt5: return {0, 31};
        case ImmKind::Csr12: return {0, 4095};
        case ImmKind::Signed21: return {-(1 << 20), (1 << 20) - 1};
    }
    return {0, 0};  // unreachable
}

// Every ASTERIA-32 instruction. The enumerator order is the row order of kInstructionSet,
// so info(op) is a direct array index (checked at compile time below).
enum class Op : u8 {
    // OP group (opcode 0x01), R-type, selected by funct11
    Add, Sub, And, Or, Xor, Nor, Sll, Srl, Sra, Slt, Sltu,
    // MULDIV group (opcode 0x02)
    Mul, Mulh, Mulhu, Div, Divu, Rem, Remu,
    // Immediate ALU (0x04-0x0E)
    Addi, Slti, Sltiu, Andi, Ori, Xori, Slli, Srli, Srai, Lui, Auipc,
    // Loads (0x10-0x15) and stores (0x18-0x1A)
    Lb, Lh, Lw, Lbu, Lhu,
    Sb, Sh, Sw,
    // AMO group (0x1C)
    Swap,
    // Branches (0x20-0x25) and jumps (0x28-0x29)
    Beq, Bne, Blt, Bge, Bltu, Bgeu,
    Jal, Jalr,
    // SYSTEM group (0x30)
    Ecall, Ebreak, Eret, Wfi, Halt, Tlbflush, FenceI,
    // CSR access (0x31-0x36)
    Csrrw, Csrrs, Csrrc, Csrrwi, Csrrsi, Csrrci,
};

struct OpInfo {
    Op op;
    std::string_view mnemonic;  // upper case, as printed by the disassembler
    u8 opcode;                  // inst[5:0]
    u16 funct;                  // inst[31:21] for R-format groups; 0 otherwise
    Form form;
    bool kernel_only;           // executing it in User mode raises PrivilegeViolation (§6)
};

// Spec §3 (opcode map) and §3 "funct11 values". One row per instruction.
inline constexpr auto kInstructionSet = std::to_array<OpInfo>({
    //  op            mnemonic   opcode funct form               kernel_only
    {Op::Add,      "ADD",      0x01, 0,  Form::RegRegReg,   false},
    {Op::Sub,      "SUB",      0x01, 1,  Form::RegRegReg,   false},
    {Op::And,      "AND",      0x01, 2,  Form::RegRegReg,   false},
    {Op::Or,       "OR",       0x01, 3,  Form::RegRegReg,   false},
    {Op::Xor,      "XOR",      0x01, 4,  Form::RegRegReg,   false},
    {Op::Nor,      "NOR",      0x01, 5,  Form::RegRegReg,   false},
    {Op::Sll,      "SLL",      0x01, 6,  Form::RegRegReg,   false},
    {Op::Srl,      "SRL",      0x01, 7,  Form::RegRegReg,   false},
    {Op::Sra,      "SRA",      0x01, 8,  Form::RegRegReg,   false},
    {Op::Slt,      "SLT",      0x01, 9,  Form::RegRegReg,   false},
    {Op::Sltu,     "SLTU",     0x01, 10, Form::RegRegReg,   false},

    {Op::Mul,      "MUL",      0x02, 0,  Form::RegRegReg,   false},
    {Op::Mulh,     "MULH",     0x02, 1,  Form::RegRegReg,   false},
    {Op::Mulhu,    "MULHU",    0x02, 2,  Form::RegRegReg,   false},
    {Op::Div,      "DIV",      0x02, 3,  Form::RegRegReg,   false},
    {Op::Divu,     "DIVU",     0x02, 4,  Form::RegRegReg,   false},
    {Op::Rem,      "REM",      0x02, 5,  Form::RegRegReg,   false},
    {Op::Remu,     "REMU",     0x02, 6,  Form::RegRegReg,   false},

    {Op::Addi,     "ADDI",     0x04, 0,  Form::RegRegImm,   false},
    {Op::Slti,     "SLTI",     0x05, 0,  Form::RegRegImm,   false},
    {Op::Sltiu,    "SLTIU",    0x06, 0,  Form::RegRegImm,   false},
    {Op::Andi,     "ANDI",     0x07, 0,  Form::RegRegUimm,  false},
    {Op::Ori,      "ORI",      0x08, 0,  Form::RegRegUimm,  false},
    {Op::Xori,     "XORI",     0x09, 0,  Form::RegRegUimm,  false},
    {Op::Slli,     "SLLI",     0x0A, 0,  Form::RegRegShamt, false},
    {Op::Srli,     "SRLI",     0x0B, 0,  Form::RegRegShamt, false},
    {Op::Srai,     "SRAI",     0x0C, 0,  Form::RegRegShamt, false},
    {Op::Lui,      "LUI",      0x0D, 0,  Form::RegUimm,     false},
    {Op::Auipc,    "AUIPC",    0x0E, 0,  Form::RegUimm,     false},

    {Op::Lb,       "LB",       0x10, 0,  Form::Load,        false},
    {Op::Lh,       "LH",       0x11, 0,  Form::Load,        false},
    {Op::Lw,       "LW",       0x12, 0,  Form::Load,        false},
    {Op::Lbu,      "LBU",      0x14, 0,  Form::Load,        false},
    {Op::Lhu,      "LHU",      0x15, 0,  Form::Load,        false},
    {Op::Sb,       "SB",       0x18, 0,  Form::Store,       false},
    {Op::Sh,       "SH",       0x19, 0,  Form::Store,       false},
    {Op::Sw,       "SW",       0x1A, 0,  Form::Store,       false},

    {Op::Swap,     "SWAP",     0x1C, 0,  Form::Swap,        false},

    {Op::Beq,      "BEQ",      0x20, 0,  Form::Branch,      false},
    {Op::Bne,      "BNE",      0x21, 0,  Form::Branch,      false},
    {Op::Blt,      "BLT",      0x22, 0,  Form::Branch,      false},
    {Op::Bge,      "BGE",      0x23, 0,  Form::Branch,      false},
    {Op::Bltu,     "BLTU",     0x24, 0,  Form::Branch,      false},
    {Op::Bgeu,     "BGEU",     0x25, 0,  Form::Branch,      false},
    {Op::Jal,      "JAL",      0x28, 0,  Form::Jal,         false},
    {Op::Jalr,     "JALR",     0x29, 0,  Form::Jalr,        false},

    {Op::Ecall,    "ECALL",    0x30, 0,  Form::NoOperands,  false},
    {Op::Ebreak,   "EBREAK",   0x30, 1,  Form::NoOperands,  false},
    {Op::Eret,     "ERET",     0x30, 2,  Form::NoOperands,  true},
    {Op::Wfi,      "WFI",      0x30, 3,  Form::NoOperands,  true},
    {Op::Halt,     "HALT",     0x30, 4,  Form::NoOperands,  true},
    {Op::Tlbflush, "TLBFLUSH", 0x30, 5,  Form::TlbFlush,    true},
    {Op::FenceI,   "FENCE.I",  0x30, 6,  Form::NoOperands,  false},

    {Op::Csrrw,    "CSRRW",    0x31, 0,  Form::CsrReg,      false},
    {Op::Csrrs,    "CSRRS",    0x32, 0,  Form::CsrReg,      false},
    {Op::Csrrc,    "CSRRC",    0x33, 0,  Form::CsrReg,      false},
    {Op::Csrrwi,   "CSRRWI",   0x34, 0,  Form::CsrImm,      false},
    {Op::Csrrsi,   "CSRRSI",   0x35, 0,  Form::CsrImm,      false},
    {Op::Csrrci,   "CSRRCI",   0x36, 0,  Form::CsrImm,      false},
});

inline constexpr std::size_t kOpCount = kInstructionSet.size();
inline constexpr unsigned kOpcodeCount = 64;   // 6-bit opcode field
inline constexpr unsigned kFunctLimit = 2048;  // 11-bit funct field

[[nodiscard]] constexpr const OpInfo& info(Op op) noexcept {
    return kInstructionSet[static_cast<std::size_t>(op)];
}

[[nodiscard]] constexpr Format format_of(Op op) noexcept { return format_of(info(op).form); }

// The decoder's first step: opcode -> the rows that use it. R-format group opcodes (OP,
// MULDIV, AMO, SYSTEM) own several consecutive rows told apart by funct11; every other
// opcode owns exactly one row. count = 0 marks a reserved (illegal) opcode.
struct OpcodeSlot {
    u8 first = 0;
    u8 count = 0;
};

inline constexpr std::array<OpcodeSlot, kOpcodeCount> kByOpcode = [] {
    std::array<OpcodeSlot, kOpcodeCount> slots{};
    for (std::size_t i = 0; i < kOpCount; ++i) {
        OpcodeSlot& slot = slots[kInstructionSet[i].opcode];
        if (slot.count == 0) {
            slot.first = static_cast<u8>(i);
        }
        ++slot.count;
    }
    return slots;
}();

// Finds an instruction by mnemonic, ignoring case ("addi", "ADDI", "fence.i").
[[nodiscard]] std::optional<Op> find_op(std::string_view mnemonic) noexcept;

// ---- Compile-time consistency checks: an inconsistent table does not build. ----------------

namespace detail {

[[nodiscard]] consteval bool table_is_consistent() {
    for (std::size_t i = 0; i < kOpCount; ++i) {
        const OpInfo& a = kInstructionSet[i];
        if (static_cast<std::size_t>(a.op) != i) return false;      // row order = enum order
        if (a.opcode == 0 || a.opcode >= kOpcodeCount) return false;  // 0x00 is always illegal
        if (a.funct >= kFunctLimit) return false;
        if (a.mnemonic.empty()) return false;
        for (char c : a.mnemonic) {
            const bool upper = (c >= 'A' && c <= 'Z') || c == '.';
            if (!upper) return false;
        }
        const bool r_format = format_of(a.form) == Format::R;
        if (!r_format && a.funct != 0) return false;  // only R-format words have a funct field
        for (std::size_t j = i + 1; j < kOpCount; ++j) {
            const OpInfo& b = kInstructionSet[j];
            if (a.mnemonic == b.mnemonic) return false;
            if (a.opcode == b.opcode) {
                // Sharing an opcode is only possible inside an R-format group, with distinct functs.
                if (!r_format || format_of(b.form) != Format::R) return false;
                if (a.funct == b.funct) return false;
            }
        }
    }
    // Rows sharing an opcode must be consecutive, so an OpcodeSlot can describe them.
    for (const OpcodeSlot& slot : kByOpcode) {
        for (std::size_t k = 0; k < slot.count; ++k) {
            const u8 expected = kInstructionSet[slot.first].opcode;
            if (kInstructionSet[slot.first + k].opcode != expected) return false;
        }
    }
    return true;
}

}  // namespace detail

static_assert(detail::table_is_consistent(), "kInstructionSet is inconsistent");
static_assert(kOpCount == 59, "spec §3 defines 59 instructions");
static_assert(kByOpcode[0x00].count == 0, "opcode 0x00 must stay illegal (D3)");

}  // namespace asteria::isa
