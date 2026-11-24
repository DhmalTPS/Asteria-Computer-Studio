#include "toolchain/axf/axf.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <string_view>

#include "common/assert.hpp"

namespace asteria::toolchain::axf {

namespace {

constexpr std::array<u8, 4> kMagic = {'A', 'X', 'F', '\0'};

bool symbol_less(const Symbol& a, const Symbol& b) {
    return a.value != b.value ? a.value < b.value : a.name < b.name;
}

u64 page_round_up(u64 v) { return (v + kPageSize - 1) / kPageSize * kPageSize; }

// ---- little-endian byte writer / reader -----------------------------------------------------

void put16(std::vector<u8>& out, u16 v) {
    out.push_back(static_cast<u8>(v));
    out.push_back(static_cast<u8>(v >> 8));
}
void put32(std::vector<u8>& out, u32 v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>(v >> (8 * i)));
}

class Reader {
public:
    explicit Reader(std::span<const u8> bytes) : bytes_(bytes) {}
    [[nodiscard]] u16 u16_at(std::size_t at) const {
        return static_cast<u16>(bytes_[at] | bytes_[at + 1] << 8);
    }
    [[nodiscard]] u32 u32_at(std::size_t at) const {
        return u32{bytes_[at]} | u32{bytes_[at + 1]} << 8 | u32{bytes_[at + 2]} << 16 |
               u32{bytes_[at + 3]} << 24;
    }

private:
    std::span<const u8> bytes_;
};

}  // namespace

void sort_symbols(std::vector<Symbol>& symbols) {
    std::ranges::sort(symbols, symbol_less);
}

Result<void> validate(const Executable& exe) {
    if (exe.segments.empty()) {
        return fail("executable has no segments");
    }
    bool entry_ok = false;
    for (std::size_t i = 0; i < exe.segments.size(); ++i) {
        const Segment& s = exe.segments[i];
        if (s.vaddr % kPageSize != 0) {
            return fail("segment {}: address 0x{:08X} is not page-aligned", i, s.vaddr);
        }
        if (s.mem_size == 0) {
            return fail("segment {}: size is 0", i);
        }
        if (s.data.size() > s.mem_size) {
            return fail("segment {}: {} initialized bytes exceed its size of {} bytes", i,
                        s.data.size(), s.mem_size);
        }
        if (u64{s.vaddr} + s.mem_size > 0x1'0000'0000ull) {
            return fail("segment {}: extends past the end of the address space", i);
        }
        if ((s.flags & ~flag::kAll) != 0 || (s.flags & flag::kRead) == 0) {
            return fail("segment {}: flags 0x{:X} invalid (R must be set; only R, W, X exist)", i,
                        s.flags);
        }
        if (i > 0) {
            const Segment& prev = exe.segments[i - 1];
            if (page_round_up(u64{prev.vaddr} + prev.mem_size) > s.vaddr) {
                return fail("segment {}: overlaps segment {} or is out of address order", i,
                            i - 1);
            }
        }
        if ((s.flags & flag::kExecute) && exe.entry >= s.vaddr &&
            u64{exe.entry} < u64{s.vaddr} + s.data.size()) {
            entry_ok = true;
        }
    }
    if (exe.entry % 4 != 0 || !entry_ok) {
        return fail("entry point 0x{:08X} is not a word inside an executable segment's data",
                    exe.entry);
    }

    std::set<std::string_view> names;
    for (std::size_t i = 0; i < exe.symbols.size(); ++i) {
        const Symbol& sym = exe.symbols[i];
        if (sym.name.empty() || sym.name.find('\0') != std::string::npos) {
            return fail("symbol {}: name is empty or contains a NUL byte", i);
        }
        if (!names.insert(sym.name).second) {
            return fail("symbol '{}' is defined twice", sym.name);
        }
        if (i > 0 && !symbol_less(exe.symbols[i - 1], sym)) {
            return fail("symbols are not sorted by (value, name) at '{}'", sym.name);
        }
    }
    return {};
}

Result<std::vector<u8>> write(const Executable& exe) {
    if (Result<void> ok = validate(exe); !ok) {
        return std::unexpected(ok.error());
    }

    // The string table: one NUL-terminated name per symbol, in symbol order.
    std::vector<u8> strtab;
    std::vector<u32> name_offsets;
    for (const Symbol& sym : exe.symbols) {
        name_offsets.push_back(static_cast<u32>(strtab.size()));
        strtab.insert(strtab.end(), sym.name.begin(), sym.name.end());
        strtab.push_back(0);
    }

    const std::size_t data_start = kHeaderSize + exe.segments.size() * kSegmentEntrySize +
                                   exe.symbols.size() * kSymbolEntrySize + strtab.size();
    std::vector<u8> out;
    out.insert(out.end(), kMagic.begin(), kMagic.end());
    put16(out, kVersionMajor);
    put16(out, kVersionMinor);
    put16(out, kTypeExecutable);
    put16(out, 0);  // reserved
    put32(out, exe.entry);
    put32(out, static_cast<u32>(exe.segments.size()));
    put32(out, static_cast<u32>(exe.symbols.size()));
    put32(out, static_cast<u32>(strtab.size()));
    put32(out, 0);  // reserved
    ASTERIA_ASSERT(out.size() == kHeaderSize, "AXF header size");

    std::size_t offset = data_start;
    for (const Segment& s : exe.segments) {
        put32(out, s.vaddr);
        put32(out, static_cast<u32>(offset));
        put32(out, static_cast<u32>(s.data.size()));
        put32(out, s.mem_size);
        put32(out, s.flags);
        offset += s.data.size();
    }
    for (std::size_t i = 0; i < exe.symbols.size(); ++i) {
        put32(out, name_offsets[i]);
        put32(out, exe.symbols[i].value);
        put32(out, exe.symbols[i].global ? 1u : 0u);
    }
    out.insert(out.end(), strtab.begin(), strtab.end());
    ASTERIA_ASSERT(out.size() == data_start, "AXF table sizes");
    for (const Segment& s : exe.segments) {
        out.insert(out.end(), s.data.begin(), s.data.end());
    }
    return out;
}

Result<Executable> read(std::span<const u8> bytes) {
    if (bytes.size() < kHeaderSize) {
        return fail("not an AXF file: only {} bytes", bytes.size());
    }
    if (!std::ranges::equal(bytes.first(4), kMagic)) {
        return fail("not an AXF file: bad magic number");
    }
    const Reader r{bytes};
    const u16 major = r.u16_at(4);
    const u16 minor = r.u16_at(6);
    if (major != kVersionMajor || minor != kVersionMinor) {
        return fail("unsupported AXF version {}.{} (this build reads {}.{})", major, minor,
                    kVersionMajor, kVersionMinor);
    }
    const u16 type = r.u16_at(8);
    if (type == kTypeRelocatable) {
        return fail("this is a relocatable object (.aso); linking arrives in M9");
    }
    if (type != kTypeExecutable) {
        return fail("unknown AXF file type {}", type);
    }
    if (r.u16_at(10) != 0 || r.u32_at(28) != 0) {
        return fail("reserved header fields must be 0");
    }

    Executable exe;
    exe.entry = r.u32_at(12);
    const u64 segment_count = r.u32_at(16);
    const u64 symbol_count = r.u32_at(20);
    const u64 strtab_size = r.u32_at(24);

    // Tables must fit before any arithmetic on their contents (u64: no overflow).
    const u64 segments_at = kHeaderSize;
    const u64 symbols_at = segments_at + segment_count * kSegmentEntrySize;
    const u64 strtab_at = symbols_at + symbol_count * kSymbolEntrySize;
    const u64 data_at = strtab_at + strtab_size;
    if (data_at > bytes.size()) {
        return fail("truncated file: tables need {} bytes, file has {}", data_at, bytes.size());
    }

    // Segments: canonical form packs their data in order, right after the string table.
    u64 expected_offset = data_at;
    for (u64 i = 0; i < segment_count; ++i) {
        const std::size_t at = static_cast<std::size_t>(segments_at + i * kSegmentEntrySize);
        Segment s;
        s.vaddr = r.u32_at(at);
        const u32 file_offset = r.u32_at(at + 4);
        const u32 file_size = r.u32_at(at + 8);
        s.mem_size = r.u32_at(at + 12);
        s.flags = r.u32_at(at + 16);
        if (file_offset != expected_offset) {
            return fail("segment {}: data at offset {} (expected {})", i, file_offset,
                        expected_offset);
        }
        if (u64{file_offset} + file_size > bytes.size()) {
            return fail("segment {}: data runs past the end of the file", i);
        }
        s.data.assign(bytes.begin() + file_offset, bytes.begin() + file_offset + file_size);
        expected_offset += file_size;
        exe.segments.push_back(std::move(s));
    }
    if (expected_offset != bytes.size()) {
        return fail("{} unexpected bytes at the end of the file", bytes.size() - expected_offset);
    }

    // Symbols: names are NUL-terminated strings in the string table, stored in symbol order.
    const auto strtab = bytes.subspan(static_cast<std::size_t>(strtab_at),
                                      static_cast<std::size_t>(strtab_size));
    u64 expected_name = 0;
    for (u64 i = 0; i < symbol_count; ++i) {
        const std::size_t at = static_cast<std::size_t>(symbols_at + i * kSymbolEntrySize);
        const u32 name_offset = r.u32_at(at);
        const u32 flags = r.u32_at(at + 8);
        if (name_offset != expected_name || name_offset >= strtab.size()) {
            return fail("symbol {}: bad name offset {}", i, name_offset);
        }
        if (flags > 1) {
            return fail("symbol {}: unknown flags 0x{:X}", i, flags);
        }
        const auto rest = strtab.subspan(name_offset);
        const auto nul = std::ranges::find(rest, u8{0});
        if (nul == rest.end()) {
            return fail("symbol {}: name is not NUL-terminated", i);
        }
        Symbol sym;
        sym.name.assign(rest.begin(), nul);
        sym.value = r.u32_at(at + 4);
        sym.global = flags == 1;
        expected_name = name_offset + sym.name.size() + 1;
        exe.symbols.push_back(std::move(sym));
    }
    if (expected_name != strtab.size()) {
        return fail("string table has {} unused bytes", strtab.size() - expected_name);
    }

    if (Result<void> ok = validate(exe); !ok) {
        return fail("invalid executable: {}", ok.error().message);
    }
    return exe;
}

}  // namespace asteria::toolchain::axf
