#pragma once
// Disassembler: Instruction -> assembly text.
//
// The output is the CANONICAL form of each instruction, and the assembler accepts it
// unchanged. That gives the round trip M1 promises: assemble -> disassemble -> reassemble
// produces the same machine words.
//
// Conventions (spec §11):
//   ADD R3, R1, R2          registers by number (or ABI names on request)
//   ADDI R1, R0, -2         signed immediates in decimal
//   ORI R1, R1, 0xFFFF      unsigned immediates (ANDI/ORI/XORI/LUI/AUIPC) in hex
//   LW R5, 8(R2)            memory operands as imm(base)
//   BEQ R1, R2, +3          branch/JAL targets RELATIVE, in instructions, always signed;
//                           the assembler reads a leading +/- as "relative to this instruction"
//   CSRRW R1, STATUS, R2    CSRs by name; an undefined CSR number prints as hex (0x123)

#include <string>

#include "isa/encoding.hpp"

namespace asteria::isa {

enum class RegisterNames : u8 { Numeric, Abi };

[[nodiscard]] std::string disassemble(const Instruction& instruction,
                                      RegisterNames names = RegisterNames::Numeric);

}  // namespace asteria::isa
