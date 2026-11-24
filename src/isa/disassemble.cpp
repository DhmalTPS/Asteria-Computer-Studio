#include "isa/disassemble.hpp"

#include <format>
#include <string_view>

#include "common/assert.hpp"
#include "isa/csrs.hpp"
#include "isa/registers.hpp"

namespace asteria::isa {

namespace {

std::string_view reg_name(u8 n, RegisterNames names) {
    ASTERIA_ASSERT(n < kRegisterCount, "register number out of range");
    return names == RegisterNames::Abi ? kAbiNames[n] : kNumericNames[n];
}

std::string csr_name(i32 number) {
    if (auto csr = find_csr(static_cast<u16>(number))) {
        return std::string{csr->name};
    }
    return std::format("0x{:03X}", number);
}

}  // namespace

std::string disassemble(const Instruction& in, RegisterNames names) {
    const OpInfo& row = info(in.op);
    const std::string_view m = row.mnemonic;
    const auto r = [names](u8 n) { return reg_name(n, names); };

    switch (row.form) {
        case Form::RegRegReg:
            return std::format("{} {}, {}, {}", m, r(in.rd), r(in.rs1), r(in.rs2));
        case Form::RegRegImm:
            return std::format("{} {}, {}, {}", m, r(in.rd), r(in.rs1), in.imm);
        case Form::RegRegUimm:
            return std::format("{} {}, {}, 0x{:X}", m, r(in.rd), r(in.rs1), in.imm);
        case Form::RegRegShamt:
            return std::format("{} {}, {}, {}", m, r(in.rd), r(in.rs1), in.imm);
        case Form::RegUimm:
            return std::format("{} {}, 0x{:X}", m, r(in.rd), in.imm);
        case Form::Load:
            return std::format("{} {}, {}({})", m, r(in.rd), in.imm, r(in.rs1));
        case Form::Store:
            return std::format("{} {}, {}({})", m, r(in.rs2), in.imm, r(in.rs1));
        case Form::Branch:
            return std::format("{} {}, {}, {:+}", m, r(in.rs1), r(in.rs2), in.imm);
        case Form::Jal:
            return std::format("{} {}, {:+}", m, r(in.rd), in.imm);
        case Form::Jalr:
            return std::format("{} {}, {}, {}", m, r(in.rd), r(in.rs1), in.imm);
        case Form::Swap:
            return std::format("{} {}, {}, ({})", m, r(in.rd), r(in.rs2), r(in.rs1));
        case Form::NoOperands:
            return std::string{m};
        case Form::TlbFlush:
            return std::format("{} {}, {}", m, r(in.rs1), r(in.rs2));
        case Form::CsrReg:
            return std::format("{} {}, {}, {}", m, r(in.rd), csr_name(in.imm), r(in.rs1));
        case Form::CsrImm:
            return std::format("{} {}, {}, {}", m, r(in.rd), csr_name(in.imm), in.rs1);
    }
    ASTERIA_ASSERT(false, "unhandled instruction form");
    return {};
}

}  // namespace asteria::isa
