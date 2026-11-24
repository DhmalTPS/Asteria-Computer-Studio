#pragma once
// Executable -> assembly source that assembles back to the SAME BYTES (`asteria disasm x.ast`).
//
// The round trip  source -> assemble -> listing -> assemble  must reproduce the executable
// exactly: same segments, entry and symbols. That works because every piece of the
// executable has exactly one spelling the assembler reads back:
//   - instructions use the disassembler's canonical form; branch targets stay relative (+n)
//   - a word that is not an instruction becomes `.word 0x...`
//   - data becomes `.byte` lines, .bss becomes `.space`
//   - symbols become labels at their addresses (`.global` where set)
//   - an entry point without a symbol gets a local label, `.Lentry`, which is not exported
//
// Only executables laid out the way the assembler lays them out can be listed (text, rodata,
// data, bss at their default addresses); anything else is reported as an error.

#include <string>

#include "common/result.hpp"
#include "toolchain/axf/axf.hpp"

namespace asteria::toolchain::as {

[[nodiscard]] Result<std::string> listing(const axf::Executable& exe);

}  // namespace asteria::toolchain::as
