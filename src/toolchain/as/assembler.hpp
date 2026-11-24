#pragma once
// The assembler, v1 (spec §11): one .asm file -> one executable (AXF, docs/formats/AXF.md).
//
// Two passes, because a branch may name a label defined further down:
//   pass 1  parses every line, defines labels, and computes every statement's SIZE. Sizes never
//           depend on label values (LI needs a constant; LA is always two instructions), so one
//           pass is enough to fix the layout.
//   pass 2  knows every address, evaluates operands and encodes.
//
// Layout (bare-metal mode, docs/platform/MEMORY-MAP.md section 3): .text at 0x8000_0000, then
// .rodata, .data, .bss, each non-empty section starting on a new page. Segments: text R+X,
// rodata R, data R+W, bss R+W with no file data.
//
// Errors are collected, not stopped at: the result lists every problem found (up to a limit),
// one per line, as "file:line:column: error: message".
//
// Deferred to the full assembler (M9), with a clear error: .include, .macro/.endm, .extern.

#include <string_view>

#include "common/result.hpp"
#include "toolchain/axf/axf.hpp"

namespace asteria::toolchain::as {

// At most this many errors are printed, in source order, then a count of the rest.
inline constexpr unsigned kMaxErrors = 50;

[[nodiscard]] Result<axf::Executable> assemble(std::string_view source, std::string_view file_name);

}  // namespace asteria::toolchain::as
