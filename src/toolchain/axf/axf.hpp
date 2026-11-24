#pragma once
// AXF, the Asteria eXecutable Format (D17, docs/formats/AXF.md): the container for `.ast`
// executables (and, from M9, `.aso` relocatable objects).
//
// M1 implements the EXECUTABLE subset: a header, permission-carrying segments for the loader,
// and a symbol table for the disassembler and debugger. Relocations and the line table come
// with the linker in M9.
//
// Two rules keep the format easy to trust:
//   - write() produces exactly one byte layout for a given Executable (the canonical form),
//     and read() accepts only that form. So read(write(x)) == x and write(read(b)) == b.
//   - validate() states every semantic rule once. write() refuses what it refuses, and read()
//     applies it to every file, so no tool ever sees a malformed executable.

#include <span>
#include <string>
#include <vector>

#include "common/result.hpp"
#include "common/types.hpp"

namespace asteria::toolchain::axf {

// Segment permission flags (also the order of PTE bits R, W, X, shifted down by one).
namespace flag {
inline constexpr u32 kRead = 1u << 0;
inline constexpr u32 kWrite = 1u << 1;
inline constexpr u32 kExecute = 1u << 2;
inline constexpr u32 kAll = kRead | kWrite | kExecute;
}  // namespace flag

struct Segment {
    u32 vaddr = 0;          // page-aligned load address
    u32 mem_size = 0;       // bytes in memory; the part past data.size() is zero-filled (.bss)
    u32 flags = 0;          // flag::kRead | ...
    std::vector<u8> data;   // initialized bytes (file_size = data.size())

    friend bool operator==(const Segment&, const Segment&) = default;
};

struct Symbol {
    std::string name;
    u32 value = 0;
    bool global = false;  // declared with .global

    friend bool operator==(const Symbol&, const Symbol&) = default;
};

struct Executable {
    u32 entry = 0;
    std::vector<Segment> segments;  // sorted by vaddr, non-overlapping
    std::vector<Symbol> symbols;    // sorted by (value, name), unique names

    friend bool operator==(const Executable&, const Executable&) = default;
};

// File constants (docs/formats/AXF.md).
inline constexpr u16 kVersionMajor = 1;
inline constexpr u16 kVersionMinor = 0;
inline constexpr u16 kTypeRelocatable = 1;
inline constexpr u16 kTypeExecutable = 2;
inline constexpr std::size_t kHeaderSize = 32;
inline constexpr std::size_t kSegmentEntrySize = 20;
inline constexpr std::size_t kSymbolEntrySize = 12;
inline constexpr u32 kPageSize = 4096;

// Checks the semantic rules (docs/formats/AXF.md section 3). The error names the first rule
// that fails.
[[nodiscard]] Result<void> validate(const Executable& exe);

// Sorts symbols into canonical order. Builders call this before validate()/write().
void sort_symbols(std::vector<Symbol>& symbols);

[[nodiscard]] Result<std::vector<u8>> write(const Executable& exe);
[[nodiscard]] Result<Executable> read(std::span<const u8> bytes);

}  // namespace asteria::toolchain::axf
