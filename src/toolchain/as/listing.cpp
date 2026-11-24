#include "toolchain/as/listing.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "common/memory_map.hpp"
#include "isa/disassemble.hpp"
#include "isa/encoding.hpp"
#include "isa/registers.hpp"
#include "toolchain/as/lexer.hpp"

namespace asteria::toolchain::as {

namespace {

constexpr std::size_t kSections = 4;  // text, rodata, data, bss: the assembler's order
constexpr std::array<std::string_view, kSections> kNames = {".text", ".rodata", ".data", ".bss"};
constexpr std::size_t kText = 0;
constexpr std::size_t kBss = 3;
constexpr unsigned kBytesPerLine = 16;

// Which section a segment is, judging by what the assembler produces for each.
std::optional<std::size_t> section_of(const axf::Segment& s) {
    using namespace axf::flag;
    const bool full = s.data.size() == s.mem_size;
    if (s.flags == (kRead | kExecute) && full) return 0;
    if (s.flags == kRead && full) return 1;
    if (s.flags == (kRead | kWrite) && full) return 2;
    if (s.flags == (kRead | kWrite) && s.data.empty()) return 3;
    return std::nullopt;
}

// A symbol name must read back as exactly one label token.
bool writable_as_label(std::string_view name) {
    const auto tokens = tokenize_line(name);
    return tokens && tokens->size() == 1 && (*tokens)[0].kind == TokenKind::Identifier &&
           (*tokens)[0].text == name && !name.starts_with('.') && !isa::parse_register(name);
}

}  // namespace

Result<std::string> listing(const axf::Executable& exe) {
    // 1. Map segments to sections, in the assembler's order.
    std::array<const axf::Segment*, kSections> seg{};
    std::size_t last = 0;
    for (std::size_t i = 0; i < exe.segments.size(); ++i) {
        const std::optional<std::size_t> s = section_of(exe.segments[i]);
        if (!s || seg[*s] != nullptr || (i > 0 && *s <= last)) {
            return fail("segment {} (flags 0x{:X} at 0x{:08X}) is not laid out the way the "
                        "assembler lays out sections, so it can't be listed",
                        i, exe.segments[i].flags, exe.segments[i].vaddr);
        }
        seg[*s] = &exe.segments[i];
        last = *s;
    }
    if (seg[kText] == nullptr) {
        return fail("the executable has no text segment");
    }

    // 2. The addresses the assembler would give each section; the segments must match them.
    std::array<u64, kSections> base{};
    std::array<u32, kSections> size{};
    u64 cursor = memory_map::kRamBase;
    for (std::size_t s = 0; s < kSections; ++s) {
        base[s] = cursor;
        if (seg[s] == nullptr) continue;
        if (seg[s]->vaddr != base[s]) {
            return fail("{} is at 0x{:08X}, but the assembler would place it at 0x{:08X}",
                        kNames[s], seg[s]->vaddr, base[s]);
        }
        size[s] = seg[s]->mem_size;
        cursor = (cursor + size[s] + memory_map::kPageSize - 1) / memory_map::kPageSize *
                 memory_map::kPageSize;
    }

    // 3. Labels. An address can belong to more than one section (the end of one section can be
    //    the start of the next, and an empty section has only a start), and any choice
    //    reassembles to the same value. For readability, prefer in this order: inside a
    //    section with contents, at its end, then at the start of an empty section.
    std::array<std::multimap<u32, std::string>, kSections> labels;  // offset -> names
    for (const axf::Symbol& sym : exe.symbols) {
        if (!writable_as_label(sym.name)) {
            return fail("symbol '{}' can't be written as a label", sym.name);
        }
        const u64 v = sym.value;
        const auto fits = [&](std::size_t s, int rule) {
            if (rule == 0) return seg[s] != nullptr && v >= base[s] && v < base[s] + size[s];
            if (rule == 1) return seg[s] != nullptr && v == base[s] + size[s];
            return seg[s] == nullptr && v == base[s];
        };
        bool placed = false;
        for (int rule = 0; rule < 3 && !placed; ++rule) {
            for (std::size_t s = 0; s < kSections && !placed; ++s) {
                if (!fits(s, rule) || (s == kText && (v - base[s]) % 4 != 0)) continue;
                labels[s].emplace(static_cast<u32>(v - base[s]), sym.name);
                placed = true;
            }
        }
        if (!placed) {
            return fail("symbol '{}' (0x{:08X}) is not at an address inside any section",
                        sym.name, sym.value);
        }
    }

    // 4. The entry point: default (start of .text), a symbol, or a local label.
    std::string entry_name;
    if (exe.entry != base[kText]) {
        const u32 offset = static_cast<u32>(exe.entry - base[kText]);
        const auto at = labels[kText].find(offset);
        if (at != labels[kText].end()) {
            entry_name = at->second;
        } else {
            entry_name = ".Lentry";
            labels[kText].emplace(offset, entry_name);
        }
    }

    // Symbol names by address, for branch-target comments.
    std::map<u32, std::string_view> names_at;
    for (const axf::Symbol& sym : exe.symbols) names_at.emplace(sym.value, sym.name);

    // 5. Write it out.
    std::string out;
    out += "; Listing of an Asteria executable. Assembling it gives back the same bytes.\n";
    for (const axf::Symbol& sym : exe.symbols) {
        if (sym.global) out += std::format(".global {}\n", sym.name);
    }
    if (!entry_name.empty()) out += std::format(".entry {}\n", entry_name);

    const auto emit_labels = [&](std::size_t s, u32 offset) {
        const auto [first, end] = labels[s].equal_range(offset);
        for (auto it = first; it != end; ++it) out += std::format("{}:\n", it->second);
    };

    for (std::size_t s = 0; s < kSections; ++s) {
        if (seg[s] == nullptr && labels[s].empty()) continue;
        out += std::format("\n{}\n", kNames[s]);
        if (s == kText) {
            const std::vector<u8>& d = seg[s]->data;
            for (u32 off = 0; off < size[s]; off += 4) {
                emit_labels(s, off);
                const u32 word = u32{d[off]} | u32{d[off + 1]} << 8 | u32{d[off + 2]} << 16 |
                                 u32{d[off + 3]} << 24;
                const u32 pc = static_cast<u32>(base[s] + off);
                std::string text;
                std::string note = std::format("0x{:08X}: {:08X}", pc, word);
                if (const auto in = isa::decode(word)) {
                    text = isa::disassemble(*in);
                    const isa::Form form = isa::info(in->op).form;
                    if (form == isa::Form::Branch || form == isa::Form::Jal) {
                        const u32 target = pc + static_cast<u32>(in->imm) * 4u;
                        const auto name = names_at.find(target);
                        note += std::format("  -> 0x{:08X}{}", target,
                                            name != names_at.end() ? std::format(" {}", name->second)
                                                                   : std::string{});
                    }
                } else {
                    text = std::format(".word 0x{:08X}", word);
                    note += "  (not an instruction)";
                }
                out += std::format("    {:<32} ; {}\n", text, note);
            }
            emit_labels(s, size[s]);
            continue;
        }

        // Data and bss: runs of bytes between label positions.
        std::vector<u32> cuts{0};
        for (const auto& [offset, unused] : labels[s]) cuts.push_back(offset);
        cuts.push_back(size[s]);
        u32 previous_label_offset = 0xFFFF'FFFF;
        for (std::size_t c = 0; c + 1 < cuts.size(); ++c) {
            const u32 from = cuts[c];
            const u32 to = cuts[c + 1];
            if (from != previous_label_offset) {
                emit_labels(s, from);
                previous_label_offset = from;
            }
            if (to <= from) continue;
            if (s == kBss) {
                out += std::format("    .space {}\n", to - from);
                continue;
            }
            for (u32 off = from; off < to; off += kBytesPerLine) {
                std::string line = "    .byte ";
                for (u32 k = off; k < std::min(to, off + kBytesPerLine); ++k) {
                    if (k != off) line += ", ";
                    line += std::format("0x{:02X}", seg[s]->data[k]);
                }
                out += line + '\n';
            }
        }
        if (size[s] != previous_label_offset) emit_labels(s, size[s]);
    }
    return out;
}

}  // namespace asteria::toolchain::as
