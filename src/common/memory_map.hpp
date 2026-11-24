#pragma once
// The platform's physical address map (docs/platform/MEMORY-MAP.md, decision D12).
//
// These numbers are shared by the physical bus (mem/phys), the assembler's default layout
// (toolchain) and the machine's loader. They live in common/ so that each of those modules
// reads the same constants without depending on the others.

#include "common/types.hpp"

namespace asteria::memory_map {

inline constexpr u32 kPageSize = 4096;  // 4 KiB pages (spec §8.2)

// Boot ROM: reset PC = 0 (spec §1). Readable and executable, never writable by the CPU.
inline constexpr u32 kRomBase = 0x0000'0000;
inline constexpr u32 kRomSize = 64 * 1024;

// RAM: size is configurable, in whole pages.
inline constexpr u32 kRamBase = 0x8000'0000;
inline constexpr u32 kDefaultRamSize = 64 * 1024 * 1024;
inline constexpr u32 kMaxRamSize = 1024 * 1024 * 1024;  // ends at 0xBFFF'FFFF, below MMIO

// MMIO: one 4 KiB page per device, 32-bit accesses only, never cached, never executed.
inline constexpr u32 kMmioBase = 0xF000'0000;
inline constexpr u32 kMmioSize = 1024 * 1024;

static_assert(kRamBase + kMaxRamSize <= kMmioBase, "RAM must never overlap MMIO");
static_assert(kRomBase + kRomSize <= kRamBase, "ROM must never overlap RAM");

}  // namespace asteria::memory_map
