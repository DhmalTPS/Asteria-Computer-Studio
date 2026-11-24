#include "isa/encoding.hpp"

#include <string_view>

#include "common/assert.hpp"
#include "common/bits.hpp"

namespace asteria::isa {

namespace {

// Checks one register field: in range if used, zero if not.
std::optional<Error> check_register(std::string_view mnemonic, std::string_view field_name,
                                    u8 value, bool used, std::string_view range_name = "0..31") {
    if (used) {
        if (value > 31) {
            return Error{std::format("{}: {} must be {} (got {})", mnemonic, field_name,
                                     range_name, value)};
        }
    } else if (value != 0) {
        return Error{std::format("{} has no {} operand (field must be 0, got {})", mnemonic,
                                 field_name, value)};
    }
    return std::nullopt;
}

}  // namespace

Result<u32> encode(const Instruction& in) {
    ASTERIA_ASSERT(static_cast<std::size_t>(in.op) < kOpCount, "invalid Op value");
    const OpInfo& row = info(in.op);
    const FieldUse used = fields_used(row.form);
    const bool csr_imm = row.form == Form::CsrImm;

    if (auto error = check_register(row.mnemonic, "rd", in.rd, used.rd)) {
        return std::unexpected{*error};
    }
    if (auto error = check_register(row.mnemonic, csr_imm ? "uimm5" : "rs1", in.rs1, used.rs1)) {
        return std::unexpected{*error};
    }
    if (auto error = check_register(row.mnemonic, "rs2", in.rs2, used.rs2)) {
        return std::unexpected{*error};
    }

    const ImmKind kind = imm_kind_of(row.form);
    const ImmRange range = imm_range(kind);
    if (in.imm < range.min || in.imm > range.max) {
        if (kind == ImmKind::None) {
            return fail("{} has no immediate (got {})", row.mnemonic, in.imm);
        }
        return fail("{}: immediate {} out of range {}..{}", row.mnemonic, in.imm, range.min,
                    range.max);
    }

    using bits::insert;
    const u32 imm = static_cast<u32>(in.imm);  // two's complement bit pattern
    u32 word = 0;
    word = insert(word, field::kOpcodeHi, field::kOpcodeLo, row.opcode);
    if (used.rd) word = insert(word, field::kRdHi, field::kRdLo, in.rd);
    if (used.rs1) word = insert(word, field::kRs1Hi, field::kRs1Lo, in.rs1);
    if (used.rs2) word = insert(word, field::kRs2Hi, field::kRs2Lo, in.rs2);

    switch (format_of(row.form)) {
        case Format::R:
            word = insert(word, field::kFunctHi, field::kFunctLo, row.funct);
            break;
        case Format::I:
            word = insert(word, 31, 16, imm);  // the range check above keeps this lossless
            break;
        case Format::S:
        case Format::B:
            word = insert(word, 10, 6, imm);       // imm[4:0]
            word = insert(word, 31, 21, imm >> 5);  // imm[15:5]
            break;
        case Format::J:
            word = insert(word, 31, 11, imm);  // imm[20:0]
            break;
    }

    // The encoder and decoder must agree. If they don't, that's a bug in this module.
    ASTERIA_ASSERT(decode(word) == in, "encode/decode round trip failed");
    return word;
}

}  // namespace asteria::isa
