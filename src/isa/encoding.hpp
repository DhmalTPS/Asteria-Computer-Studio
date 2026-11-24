#pragma once
// Instruction encoding and decoding: 32-bit word <-> Instruction (spec §2).
//
// decode() and encode() are exact inverses on every legal word:
//     decode(encode(i)) == i   and   encode(*decode(w)) == w
// That only works because decoding is STRICT (D3): a field an instruction doesn't use must
// be zero, otherwise the word is illegal. With a lax decoder, many words would decode to the
// same instruction, and the round trip (and the differential fuzzer built on it) would break.
//
// The decoder is constexpr: the spec's own examples (§13) are checked by the compiler at the
// bottom of this file.

#include <optional>

#include "common/bits.hpp"
#include "common/result.hpp"
#include "common/types.hpp"
#include "isa/instruction_set.hpp"

namespace asteria::isa {

// A decoded instruction. The meaning of `imm` depends on the instruction's ImmKind:
//   Signed16 / SplitSigned16 / Signed21   the sign-extended value (branch and JAL offsets
//                                         count INSTRUCTIONS: the byte offset is imm x 4)
//   Unsigned16                            0..65535 (LUI/AUIPC: the upper half, before << 16)
//   Shamt5                                the shift amount 0..31
//   Csr12                                 the CSR number
//   None                                  always 0
// Unused register fields are 0. For CSRRWI/CSRRSI/CSRRCI, `rs1` holds the 5-bit operand.
struct Instruction {
    Op op = Op::Add;
    u8 rd = 0;
    u8 rs1 = 0;
    u8 rs2 = 0;
    i32 imm = 0;

    friend constexpr bool operator==(const Instruction&, const Instruction&) = default;
};

// NOP is ADDI R0, R0, 0, deliberately not the all-zero word (spec §11.2, D3).
inline constexpr u32 kNop = 0x0000'0004;

// Field positions (spec §2). Register fields never move between formats (D3).
namespace field {
inline constexpr unsigned kOpcodeHi = 5, kOpcodeLo = 0;
inline constexpr unsigned kRdHi = 10, kRdLo = 6;
inline constexpr unsigned kRs1Hi = 15, kRs1Lo = 11;
inline constexpr unsigned kRs2Hi = 20, kRs2Lo = 16;
inline constexpr unsigned kFunctHi = 31, kFunctLo = 21;
}  // namespace field

// Returns the instruction a word encodes, or nullopt if the word is illegal: a reserved
// opcode, an undefined funct11 in a group, or a non-zero field the instruction doesn't use.
// A nullopt here is what makes the CPU raise IllegalInstruction.
[[nodiscard]] constexpr std::optional<Instruction> decode(u32 word) noexcept {
    using bits::extract;
    using bits::sign_extend;

    const OpcodeSlot slot = kByOpcode[extract(word, field::kOpcodeHi, field::kOpcodeLo)];
    if (slot.count == 0) {
        return std::nullopt;  // reserved opcode, including 0x00
    }

    // Find the row. A group opcode needs funct11 to tell its members apart.
    const OpInfo* row = &kInstructionSet[slot.first];
    if (format_of(row->form) == Format::R) {
        const u32 funct = extract(word, field::kFunctHi, field::kFunctLo);
        row = nullptr;
        for (unsigned k = 0; k < slot.count; ++k) {
            if (kInstructionSet[slot.first + k].funct == funct) {
                row = &kInstructionSet[slot.first + k];
                break;
            }
        }
        if (row == nullptr) {
            return std::nullopt;  // undefined funct11 in this group
        }
    }

    const Format format = format_of(row->form);
    const FieldUse present = fields_present(format);
    const FieldUse used = fields_used(row->form);
    const u32 rd = extract(word, field::kRdHi, field::kRdLo);
    const u32 rs1 = extract(word, field::kRs1Hi, field::kRs1Lo);
    const u32 rs2 = extract(word, field::kRs2Hi, field::kRs2Lo);

    // Strict decoding (D3): a register field that exists in this format but is unused by this
    // instruction must be zero.
    if ((present.rd && !used.rd && rd != 0) || (present.rs1 && !used.rs1 && rs1 != 0) ||
        (present.rs2 && !used.rs2 && rs2 != 0)) {
        return std::nullopt;
    }

    Instruction in;
    in.op = row->op;
    in.rd = used.rd ? static_cast<u8>(rd) : u8{0};
    in.rs1 = used.rs1 ? static_cast<u8>(rs1) : u8{0};
    in.rs2 = used.rs2 ? static_cast<u8>(rs2) : u8{0};

    const u32 imm16 = extract(word, 31, 16);
    switch (imm_kind_of(row->form)) {
        case ImmKind::None:
            break;
        case ImmKind::Signed16:
            in.imm = static_cast<i32>(sign_extend(imm16, 16));
            break;
        case ImmKind::Unsigned16:
            in.imm = static_cast<i32>(imm16);
            break;
        case ImmKind::Shamt5:
            if ((imm16 >> 5) != 0) {
                return std::nullopt;  // spec §4.3: imm[15:5] must be 0
            }
            in.imm = static_cast<i32>(imm16);
            break;
        case ImmKind::Csr12:
            if ((imm16 >> 12) != 0) {
                return std::nullopt;  // spec §4.8: imm[15:12] must be 0
            }
            in.imm = static_cast<i32>(imm16);
            break;
        case ImmKind::SplitSigned16: {
            // imm[15:5] sits in inst[31:21], imm[4:0] in inst[10:6] (where rd would be), so
            // that rs1 and rs2 keep their fixed positions.
            const u32 joined = (extract(word, 31, 21) << 5) | extract(word, 10, 6);
            in.imm = static_cast<i32>(sign_extend(joined, 16));
            break;
        }
        case ImmKind::Signed21:
            in.imm = static_cast<i32>(sign_extend(extract(word, 31, 11), 21));
            break;
    }
    return in;
}

// Builds the machine word for an instruction. Fails (with a message naming the field) if a
// register number is above 31, the immediate is out of range for its kind, or a field the
// instruction doesn't use is non-zero. The assembler reports these messages to the user.
[[nodiscard]] Result<u32> encode(const Instruction& instruction);

// ---- The spec's encoding examples (§13), checked at compile time. ----------------------------

static_assert(decode(0x000A'0044) == Instruction{Op::Addi, 1, 0, 0, 10});  // ADDI R1, R0, 10
static_assert(decode(0x0002'08C1) == Instruction{Op::Add, 3, 1, 2, 0});    // ADD R3, R1, R2
static_assert(decode(0x0002'08E0) == Instruction{Op::Beq, 0, 1, 2, 3});    // BEQ R1, R2, +3
static_assert(decode(kNop) == Instruction{Op::Addi, 0, 0, 0, 0});          // NOP
static_assert(!decode(0x0000'0000).has_value());                           // zero word: illegal

}  // namespace asteria::isa
