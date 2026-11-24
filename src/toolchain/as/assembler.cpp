#include "toolchain/as/assembler.hpp"

#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "common/ascii.hpp"
#include "common/assert.hpp"
#include "common/memory_map.hpp"
#include "isa/csrs.hpp"
#include "isa/encoding.hpp"
#include "isa/instruction_set.hpp"
#include "isa/registers.hpp"
#include "toolchain/as/lexer.hpp"

namespace asteria::toolchain::as {

namespace {

using isa::Form;
using isa::ImmKind;
using isa::Instruction;
using isa::Op;

// ---- Sections -------------------------------------------------------------------------------

enum class Section : u8 { Text, Rodata, Data, Bss };
inline constexpr std::size_t kSectionCount = 4;
constexpr std::array<std::string_view, kSectionCount> kSectionNames = {".text", ".rodata",
                                                                       ".data", ".bss"};
constexpr std::array<u32, kSectionCount> kSectionFlags = {
    axf::flag::kRead | axf::flag::kExecute, axf::flag::kRead,
    axf::flag::kRead | axf::flag::kWrite, axf::flag::kRead | axf::flag::kWrite};

std::size_t index(Section s) { return static_cast<std::size_t>(s); }
std::string_view name(Section s) { return kSectionNames[index(s)]; }

// ---- Errors with a column -------------------------------------------------------------------

struct Diag {
    unsigned column;
    std::string message;
};
template <class T>
using Parsed = std::expected<T, Diag>;

template <class... Args>
std::unexpected<Diag> error_at(unsigned column, std::format_string<Args...> format,
                               Args&&... args) {
    return std::unexpected<Diag>{Diag{column, std::format(format, std::forward<Args>(args)...)}};
}

using Tokens = std::span<const Token>;

struct Operand {
    Tokens tokens;
    unsigned column;
};

// ---- Statements: what pass 1 records for pass 2 ------------------------------------------------

enum class StmtKind : u8 { Instruction, Data, Fill };

struct Statement {
    StmtKind kind = StmtKind::Fill;
    unsigned line = 0;
    unsigned column = 0;  // of the mnemonic or directive
    Section section = Section::Text;
    u32 offset = 0;       // within the section
    u32 size = 0;         // bytes; fixed in pass 1
    std::string mnemonic;                                  // upper case (instructions)
    unsigned data_width = 0;                               // .word 4, .half 2, .byte 1
    std::vector<u8> bytes;                                 // .ascii / .asciz, resolved in pass 1
    std::vector<Token> tokens;                             // the line's tokens
    std::vector<std::pair<std::size_t, std::size_t>> ops;  // operand token ranges [begin, end)

    [[nodiscard]] std::vector<Operand> operands() const {
        std::vector<Operand> out;
        for (const auto& [b, e] : ops) {
            out.push_back({Tokens{tokens}.subspan(b, e - b), tokens[b].column});
        }
        return out;
    }
};

struct SymbolDef {
    bool is_label = true;  // false: an .equ constant
    Section section = Section::Text;
    u32 offset = 0;
    i64 value = 0;  // constants
    unsigned line = 0;
};

struct Request {  // .global / .entry, checked at the end
    std::string name;
    unsigned line;
    unsigned column;
};

// ---- Operand syntax per form (used in error messages) ------------------------------------------

struct FormSyntax {
    unsigned count;
    std::string_view text;
};

FormSyntax form_syntax(Form form) {
    switch (form) {
        case Form::RegRegReg: return {3, "rd, rs1, rs2"};
        case Form::RegRegImm: return {3, "rd, rs1, imm"};
        case Form::RegRegUimm: return {3, "rd, rs1, imm"};
        case Form::RegRegShamt: return {3, "rd, rs1, shamt"};
        case Form::RegUimm: return {2, "rd, imm"};
        case Form::Load: return {2, "rd, offset(base)"};
        case Form::Store: return {2, "rs2, offset(base)"};
        case Form::Branch: return {3, "rs1, rs2, target"};
        case Form::Jal: return {2, "rd, target"};
        case Form::Jalr: return {3, "rd, rs1, imm"};
        case Form::Swap: return {3, "rd, rs2, (rs1)"};
        case Form::NoOperands: return {0, "no operands"};
        case Form::TlbFlush: return {2, "rs1, rs2"};
        case Form::CsrReg: return {3, "rd, csr, rs1"};
        case Form::CsrImm: return {3, "rd, csr, uimm5"};
    }
    return {0, ""};
}

// Pseudo-instructions (spec §11.2). `size` is in instructions; LI's depends on its value.
struct Pseudo {
    std::string_view name;
    unsigned count;
    std::string_view syntax;
};
constexpr std::array kPseudos = std::to_array<Pseudo>({
    {"NOP", 0, "no operands"},   {"MOV", 2, "rd, rs"},       {"LI", 2, "rd, value"},
    {"LA", 2, "rd, symbol"},     {"NOT", 2, "rd, rs"},       {"NEG", 2, "rd, rs"},
    {"SEQZ", 2, "rd, rs"},       {"SNEZ", 2, "rd, rs"},      {"BEQZ", 2, "rs, target"},
    {"BNEZ", 2, "rs, target"},   {"BGT", 3, "rs1, rs2, target"}, {"BLE", 3, "rs1, rs2, target"},
    {"BGTU", 3, "rs1, rs2, target"}, {"BLEU", 3, "rs1, rs2, target"}, {"J", 1, "target"},
    {"CALL", 1, "target"},       {"JR", 1, "rs"},            {"RET", 0, "no operands"},
    {"CSRR", 2, "rd, csr"},      {"CSRW", 2, "csr, rs"},
});

const Pseudo* find_pseudo(std::string_view upper, std::size_t operand_count) {
    // JAL with ONE operand is the pseudo "JAL R31, target"; with two it is the real JAL.
    static constexpr Pseudo kJal{"JAL", 1, "target"};
    if (upper == "JAL") {
        return operand_count == 1 ? &kJal : nullptr;
    }
    for (const Pseudo& p : kPseudos) {
        if (p.name == upper) return &p;
    }
    return nullptr;
}

// LI rd, value (spec §11.2): one instruction when it can, two otherwise.
std::vector<Instruction> expand_li(u8 rd, u32 value) {
    const auto as_signed = static_cast<i32>(value);
    if (as_signed >= -32768 && as_signed <= 32767) {
        return {Instruction{Op::Addi, rd, 0, 0, as_signed}};
    }
    const auto upper = static_cast<i32>(value >> 16);
    if ((value & 0xFFFF) == 0) {
        return {Instruction{Op::Lui, rd, 0, 0, upper}};
    }
    return {Instruction{Op::Lui, rd, 0, 0, upper},
            Instruction{Op::Ori, rd, rd, 0, static_cast<i32>(value & 0xFFFF)}};
}

// A 32-bit value may be written signed or unsigned: -1 and 0xFFFFFFFF are the same word.
constexpr i64 kMin32 = -(i64{1} << 31);
constexpr i64 kMax32 = (i64{1} << 32) - 1;

// ---- The assembler ---------------------------------------------------------------------------

class Assembler {
public:
    Assembler(std::string_view source, std::string_view file) : source_(source), file_(file) {}

    Result<axf::Executable> run() {
        // Pass 2 runs even after pass-1 errors, so one build reports every problem. That is
        // safe: a statement that failed in pass 1 was never recorded, so sizes still agree.
        pass1();
        layout();
        pass2();
        if (errors_.empty()) finish();
        if (!errors_.empty()) {
            // Pass 2 finds its errors after pass 1 has finished, so sort them back into
            // source order. Stable: two errors at one spot keep the order they were found in.
            std::ranges::stable_sort(errors_, {}, [](const Located& e) {
                return std::pair{e.line, e.column};
            });
            std::string all;
            for (std::size_t i = 0; i < errors_.size() && i < kMaxErrors; ++i) {
                if (!all.empty()) all += '\n';
                all += errors_[i].text;
            }
            if (errors_.size() > kMaxErrors) {
                all += std::format("\n{}: {} errors in all; only the first {} are shown", file_,
                                   errors_.size(), kMaxErrors);
            }
            return std::unexpected(Error{all});
        }
        return std::move(exe_);
    }

private:
    // ---- diagnostics -------------------------------------------------------------------
    void report(unsigned line, unsigned column, std::string_view message) {
        errors_.push_back(
            {line, column, std::format("{}:{}:{}: error: {}", file_, line, column, message)});
    }
    void report(unsigned line, const Diag& d) { report(line, d.column, d.message); }

    // ---- pass 1 --------------------------------------------------------------------------
    void pass1() {
        unsigned line = 0;
        std::size_t pos = 0;
        while (pos <= source_.size()) {
            const std::size_t nl = source_.find('\n', pos);
            const std::size_t end = nl == std::string_view::npos ? source_.size() : nl;
            ++line;
            pass1_line(line, source_.substr(pos, end - pos));
            if (nl == std::string_view::npos) break;
            pos = nl + 1;
        }
    }

    void pass1_line(unsigned line, std::string_view text) {
        auto lexed = tokenize_line(text);
        if (!lexed) {
            report(line, lexed.error().column, lexed.error().message);
            return;
        }
        std::vector<Token> tokens = std::move(*lexed);

        std::size_t i = 0;
        while (i + 1 < tokens.size() && tokens[i].kind == TokenKind::Identifier &&
               tokens[i + 1].kind == TokenKind::Colon) {
            define_label(line, tokens[i]);
            i += 2;
        }
        if (i == tokens.size()) return;
        if (tokens[i].kind != TokenKind::Identifier) {
            report(line, tokens[i].column,
                   std::format("expected an instruction or directive, got '{}'", tokens[i].text));
            return;
        }

        Statement st;
        st.line = line;
        st.column = tokens[i].column;
        st.section = section_;
        st.offset = size_[index(section_)];
        const std::string_view word = tokens[i].text;

        // Split the operands at top-level commas.
        std::size_t begin = i + 1;
        int depth = 0;
        for (std::size_t k = i + 1; k <= tokens.size(); ++k) {
            const bool at_end = k == tokens.size();
            if (!at_end && tokens[k].kind == TokenKind::LParen) ++depth;
            if (!at_end && tokens[k].kind == TokenKind::RParen) --depth;
            if (at_end || (depth == 0 && tokens[k].kind == TokenKind::Comma)) {
                if (k == begin) {
                    if (at_end && begin == i + 1) break;  // no operands at all
                    report(line, at_end ? tokens[k - 1].column : tokens[k].column,
                           "missing operand");
                    return;
                }
                st.ops.emplace_back(begin, k);
                begin = k + 1;
            }
        }
        st.tokens = std::move(tokens);

        if (word.starts_with('.')) {
            directive(std::move(st), ascii::to_lower(word));
        } else {
            instruction(std::move(st), ascii::to_upper(word));
        }
    }

    void define_label(unsigned line, const Token& t) {
        const std::string_view label = t.text;
        if (isa::parse_register(label)) {
            report(line, t.column, std::format("'{}' is a register name and can't be a label", label));
            return;
        }
        if (label.starts_with('.') && !label.starts_with(".L")) {
            report(line, t.column,
                   std::format("label '{}': only local labels (.L...) may start with '.'", label));
            return;
        }
        if (const auto it = symbols_.find(label); it != symbols_.end()) {
            report(line, t.column,
                   std::format("'{}' is already defined on line {}", label, it->second.line));
            return;
        }
        symbols_.emplace(std::string{label},
                         SymbolDef{true, section_, size_[index(section_)], 0, line});
    }

    // Grows the current section; false (with an error) if it would pass the RAM limit.
    bool grow(const Statement& st, u64 bytes) {
        u32& size = size_[index(st.section)];
        if (u64{size} + bytes > memory_map::kMaxRamSize) {
            report(st.line, st.column,
                   std::format("{} would grow past {} bytes", name(st.section),
                               memory_map::kMaxRamSize));
            return false;
        }
        size += static_cast<u32>(bytes);
        return true;
    }

    void directive(Statement st, const std::string& d) {
        const std::vector<Operand> ops = st.operands();
        const auto expect_count = [&](std::size_t n, std::string_view syntax) {
            if (ops.size() == n) return true;
            report(st.line, st.column,
                   std::format("'{}' expects {} operand{} ({}), got {}", d, n, n == 1 ? "" : "s",
                               syntax, ops.size()));
            return false;
        };

        for (std::size_t s = 0; s < kSectionCount; ++s) {
            if (d == kSectionNames[s]) {
                if (expect_count(0, "none")) section_ = static_cast<Section>(s);
                return;
            }
        }
        if (d == ".global" || d == ".entry") {
            if (ops.empty() || (d == ".entry" && ops.size() != 1)) {
                report(st.line, st.column,
                       d == ".entry" ? "'.entry' expects one label" : "'.global' expects labels");
                return;
            }
            for (const Operand& op : ops) {
                if (op.tokens.size() != 1 || op.tokens[0].kind != TokenKind::Identifier) {
                    report(st.line, op.column, std::format("'{}' expects a label name", d));
                    continue;
                }
                Request r{std::string{op.tokens[0].text}, st.line, op.column};
                if (d == ".global") {
                    globals_.push_back(std::move(r));
                } else if (entry_) {
                    report(st.line, st.column,
                           std::format("'.entry' was already given on line {}", entry_->line));
                } else {
                    entry_ = std::move(r);
                }
            }
            return;
        }
        if (d == ".equ") {
            if (!expect_count(2, "name, value")) return;
            const Operand& n = ops[0];
            if (n.tokens.size() != 1 || n.tokens[0].kind != TokenKind::Identifier) {
                report(st.line, n.column, "'.equ' expects a name first");
                return;
            }
            const std::string_view sym = n.tokens[0].text;
            if (isa::parse_register(sym) || sym.starts_with('.')) {
                report(st.line, n.column, std::format("'{}' can't be used as a constant name", sym));
                return;
            }
            const Parsed<i64> v = eval(ops[1], false);
            if (!v) return report(st.line, v.error());
            if (const auto it = symbols_.find(sym); it != symbols_.end()) {
                report(st.line, n.column,
                       std::format("'{}' is already defined on line {}", sym, it->second.line));
                return;
            }
            symbols_.emplace(std::string{sym}, SymbolDef{false, Section::Text, 0, *v, st.line});
            return;
        }
        if (d == ".word" || d == ".half" || d == ".byte" || d == ".ascii" || d == ".asciz") {
            if (st.section == Section::Bss) {
                report(st.line, st.column,
                       std::format("'{}' puts bytes in the file, but .bss has none: use .space", d));
                return;
            }
            if (ops.empty()) {
                report(st.line, st.column, std::format("'{}' expects at least one value", d));
                return;
            }
            st.kind = StmtKind::Data;
            if (d == ".ascii" || d == ".asciz") {
                for (const Operand& op : ops) {
                    if (op.tokens.size() != 1 || op.tokens[0].kind != TokenKind::String) {
                        report(st.line, op.column, std::format("'{}' expects \"strings\"", d));
                        return;
                    }
                    st.bytes.insert(st.bytes.end(), op.tokens[0].string.begin(),
                                    op.tokens[0].string.end());
                    if (d == ".asciz") st.bytes.push_back(0);
                }
                st.size = static_cast<u32>(st.bytes.size());
            } else {
                st.data_width = d == ".word" ? 4 : d == ".half" ? 2 : 1;
                if (st.offset % st.data_width != 0) {
                    report(st.line, st.column,
                           std::format("'{}' needs a {}-byte-aligned offset, but {} is at {}; "
                                       "add '.align {}' before it",
                                       d, st.data_width, name(st.section), st.offset,
                                       st.data_width == 4 ? 2 : 1));
                    return;
                }
                st.size = static_cast<u32>(ops.size() * st.data_width);
            }
            if (grow(st, st.size)) statements_.push_back(std::move(st));
            return;
        }
        if (d == ".space" || d == ".align") {
            if (!expect_count(1, d == ".space" ? "byte count" : "power of two")) return;
            const Parsed<i64> v = eval(ops[0], false);
            if (!v) return report(st.line, v.error());
            u64 bytes = 0;
            if (d == ".space") {
                if (*v < 0 || *v > memory_map::kMaxRamSize) {
                    report(st.line, ops[0].column, std::format("'.space {}' is out of range", *v));
                    return;
                }
                bytes = static_cast<u64>(*v);
            } else {
                if (*v < 0 || *v > 12) {
                    report(st.line, ops[0].column,
                           std::format("'.align {}': the power must be 0..12 (1 byte to a page)", *v));
                    return;
                }
                const u64 alignment = u64{1} << *v;
                bytes = (alignment - st.offset % alignment) % alignment;
            }
            st.kind = StmtKind::Fill;
            st.size = static_cast<u32>(bytes);
            if (grow(st, bytes)) statements_.push_back(std::move(st));
            return;
        }
        if (d == ".include" || d == ".macro" || d == ".endm" || d == ".extern") {
            report(st.line, st.column,
                   std::format("'{}' is not supported yet: it arrives with the full assembler (M9)", d));
            return;
        }
        report(st.line, st.column, std::format("unknown directive '{}'", d));
    }

    void instruction(Statement st, std::string upper) {
        if (st.section != Section::Text) {
            report(st.line, st.column,
                   std::format("instructions belong in .text (this is {})", name(st.section)));
            return;
        }
        if (st.offset % 4 != 0) {
            report(st.line, st.column,
                   std::format("instruction at offset {} of .text is not word-aligned; add "
                               "'.align 2' before it",
                               st.offset));
            return;
        }
        u32 count = 1;
        if (const Pseudo* p = find_pseudo(upper, st.ops.size())) {
            if (p->name == "LA") count = 2;
            if (p->name == "LI") {
                const std::vector<Operand> ops = st.operands();
                if (ops.size() != 2) {
                    report(st.line, st.column,
                           std::format("LI expects 2 operands (rd, value), got {}", ops.size()));
                    return;
                }
                const Parsed<u32> v = li_value(ops[1]);
                if (!v) return report(st.line, v.error());
                count = static_cast<u32>(expand_li(0, *v).size());
            }
        } else if (!isa::find_op(upper)) {
            report(st.line, st.column, std::format("unknown instruction '{}'", upper));
            return;
        }
        st.kind = StmtKind::Instruction;
        st.mnemonic = std::move(upper);
        st.size = 4 * count;
        if (grow(st, st.size)) statements_.push_back(std::move(st));
    }

    // ---- layout ---------------------------------------------------------------------------
    void layout() {
        u64 cursor = memory_map::kRamBase;
        for (std::size_t s = 0; s < kSectionCount; ++s) {
            base_[s] = cursor;
            if (size_[s] > 0) {
                cursor = (cursor + size_[s] + memory_map::kPageSize - 1) / memory_map::kPageSize *
                         memory_map::kPageSize;
            }
        }
        if (cursor > u64{memory_map::kRamBase} + memory_map::kMaxRamSize) {
            report(1, 1, std::format("the program needs {} bytes, more than the largest RAM ({})",
                                     cursor - memory_map::kRamBase, memory_map::kMaxRamSize));
        }
        for (std::size_t s = 0; s < kSectionCount; ++s) {
            bytes_[s].assign(size_[s], 0);
        }
    }

    [[nodiscard]] u32 address_of(const SymbolDef& def) const {
        return static_cast<u32>(base_[index(def.section)] + def.offset);
    }

    // ---- expressions -----------------------------------------------------------------------
    // value := term (('+' | '-') term)*, term := ('+' | '-')* (number | symbol)
    // `labels` = false in pass 1 and wherever a constant is required.
    Parsed<i64> eval(const Operand& op, bool labels) const {
        const Tokens t = op.tokens;
        if (t.empty()) return error_at(op.column, "expected a value");
        i64 total = 0;
        std::size_t i = 0;
        while (true) {
            i64 sign = 1;
            if (i > 0) {
                if (t[i].kind != TokenKind::Plus && t[i].kind != TokenKind::Minus) {
                    return error_at(t[i].column, "expected '+' or '-' before '{}'", t[i].text);
                }
            }
            while (i < t.size() && (t[i].kind == TokenKind::Plus || t[i].kind == TokenKind::Minus)) {
                if (t[i].kind == TokenKind::Minus) sign = -sign;
                ++i;
            }
            if (i >= t.size()) return error_at(t.back().column, "the value ends after an operator");
            const Parsed<i64> term = eval_term(t[i], labels);
            if (!term) return std::unexpected(term.error());
            total += sign * *term;
            if (total > (i64{1} << 48) || total < -(i64{1} << 48)) {
                return error_at(op.column, "value is too large");
            }
            if (++i == t.size()) return total;
        }
    }

    Parsed<i64> eval_term(const Token& t, bool labels) const {
        if (t.kind == TokenKind::Number) return static_cast<i64>(t.number);
        if (t.kind != TokenKind::Identifier) {
            return error_at(t.column, "unexpected '{}' in a value", t.text);
        }
        if (isa::parse_register(t.text)) {
            return error_at(t.column, "'{}' is a register, but a value is expected here", t.text);
        }
        const auto it = symbols_.find(t.text);
        if (it == symbols_.end()) {
            if (!labels) {
                return error_at(t.column,
                                "'{}' is not a constant defined above (.equ); a constant is "
                                "required here",
                                t.text);
            }
            return error_at(t.column, "undefined symbol '{}'", t.text);
        }
        if (!it->second.is_label) return it->second.value;
        if (!labels) {
            return error_at(t.column,
                            "'{}' is a label; a constant is required here (use LA for addresses)",
                            t.text);
        }
        return i64{address_of(it->second)};
    }

    Parsed<u32> li_value(const Operand& op) const {
        const Parsed<i64> v = eval(op, false);
        if (!v) return std::unexpected(v.error());
        if (*v < kMin32 || *v > kMax32) {
            return error_at(op.column, "LI: value {} does not fit in 32 bits", *v);
        }
        return static_cast<u32>(*v);
    }

    // ---- operands ------------------------------------------------------------------------------
    static Parsed<u8> reg(const Operand& op) {
        if (op.tokens.size() == 1 && op.tokens[0].kind == TokenKind::Identifier) {
            if (const auto r = isa::parse_register(op.tokens[0].text)) return *r;
        }
        return error_at(op.column, "expected a register (R0-R31 or an ABI name), got '{}'",
                        text_of(op));
    }

    static std::string text_of(const Operand& op) {
        std::string s;
        for (const Token& t : op.tokens) s += t.text;
        return s;
    }

    // %hi(x), %lo(x), %hi_adj(x): a 16-bit field computed from a 32-bit value (spec §11.2).
    Parsed<u32> relocation_field(const Operand& op) const {
        const Tokens t = op.tokens;
        if (t.size() < 5 || t[1].kind != TokenKind::Identifier || t[2].kind != TokenKind::LParen ||
            t.back().kind != TokenKind::RParen) {
            return error_at(op.column, "expected %hi(value), %lo(value) or %hi_adj(value)");
        }
        const std::string fn = ascii::to_lower(t[1].text);
        if (fn != "hi" && fn != "lo" && fn != "hi_adj") {
            return error_at(t[1].column, "unknown operator '%{}' (use %hi, %lo or %hi_adj)",
                            t[1].text);
        }
        const Operand inner{t.subspan(3, t.size() - 4), t[3].column};
        const Parsed<i64> v = eval(inner, true);
        if (!v) return std::unexpected(v.error());
        if (*v < kMin32 || *v > kMax32) {
            return error_at(inner.column, "value {} does not fit in 32 bits", *v);
        }
        const auto x = static_cast<u32>(*v);
        if (fn == "hi") return x >> 16;
        if (fn == "lo") return x & 0xFFFFu;
        return ((x + 0x8000u) >> 16) & 0xFFFFu;  // %hi_adj: pairs with a sign-extended %lo
    }

    Parsed<i32> immediate(const Operand& op, ImmKind kind, std::string_view mnemonic) const {
        if (op.tokens.empty()) return error_at(op.column, "expected a value");
        if (op.tokens[0].kind == TokenKind::Percent) {
            const Parsed<u32> field = relocation_field(op);
            if (!field) return std::unexpected(field.error());
            // The operator yields a raw 16-bit field: a signed context reads it sign-extended.
            if (kind == ImmKind::Signed16) return static_cast<i32>(static_cast<i16>(*field));
            if (kind == ImmKind::Unsigned16) return static_cast<i32>(*field);
            return error_at(op.column, "{}: %hi/%lo give a 16-bit field, which fits only a "
                                       "16-bit immediate", mnemonic);
        }
        const Parsed<i64> v = eval(op, true);
        if (!v) return std::unexpected(v.error());
        const isa::ImmRange range = isa::imm_range(kind);
        if (*v < range.min || *v > range.max) {
            return error_at(op.column, "{}: immediate {} out of range {}..{}", mnemonic, *v,
                            range.min, range.max);
        }
        return static_cast<i32>(*v);
    }

    struct Memory {
        u8 base;
        i32 offset;
    };
    Parsed<Memory> memory(const Operand& op, std::string_view mnemonic) const {
        const Tokens t = op.tokens;
        const std::size_t n = t.size();
        if (n < 3 || t[n - 1].kind != TokenKind::RParen || t[n - 3].kind != TokenKind::LParen) {
            return error_at(op.column, "expected a memory operand like 8(sp) or (a0), got '{}'",
                            text_of(op));
        }
        const Parsed<u8> base = reg(Operand{t.subspan(n - 2, 1), t[n - 2].column});
        if (!base) return std::unexpected(base.error());
        if (n == 3) return Memory{*base, 0};
        const Parsed<i32> offset = immediate(Operand{t.first(n - 3), op.column}, ImmKind::Signed16,
                                             mnemonic);
        if (!offset) return std::unexpected(offset.error());
        return Memory{*base, *offset};
    }

    // A branch/JAL target: "+n"/"-n" counts instructions; anything else is an address.
    Parsed<i32> target(const Operand& op, u32 pc, ImmKind kind, std::string_view mnemonic) const {
        if (op.tokens.empty()) return error_at(op.column, "expected a target");
        i64 count = 0;
        if (op.tokens[0].kind == TokenKind::Plus || op.tokens[0].kind == TokenKind::Minus) {
            const Parsed<i64> v = eval(op, false);
            if (!v) return std::unexpected(v.error());
            count = *v;
        } else {
            const Parsed<i64> address = eval(op, true);
            if (!address) return std::unexpected(address.error());
            const i64 distance = *address - i64{pc};
            if (distance % 4 != 0) {
                return error_at(op.column, "{}: target 0x{:08X} is not a whole number of "
                                           "instructions away", mnemonic, *address);
            }
            count = distance / 4;
        }
        const isa::ImmRange range = isa::imm_range(kind);
        if (count < range.min || count > range.max) {
            return error_at(op.column, "{}: the target is {} instructions away; the reach is "
                                       "{}..{}", mnemonic, count, range.min, range.max);
        }
        return static_cast<i32>(count);
    }

    Parsed<i32> csr(const Operand& op) const {
        if (op.tokens.size() == 1 && op.tokens[0].kind == TokenKind::Identifier) {
            if (const auto c = isa::find_csr(op.tokens[0].text)) {
                return static_cast<i32>(c->csr);
            }
        }
        const Parsed<i64> v = eval(op, false);
        if (!v) {
            return error_at(op.column, "expected a CSR name (such as STATUS) or number, got '{}'",
                            text_of(op));
        }
        if (*v < 0 || *v > 4095) {
            return error_at(op.column, "CSR number {} out of range 0..4095", *v);
        }
        return static_cast<i32>(*v);
    }

    Parsed<i32> uimm5(const Operand& op, std::string_view mnemonic) const {
        const Parsed<i64> v = eval(op, false);
        if (!v) return std::unexpected(v.error());
        if (*v < 0 || *v > 31) {
            return error_at(op.column, "{}: uimm5 must be 0..31 (got {})", mnemonic, *v);
        }
        return static_cast<i32>(*v);
    }

    // ---- pass 2: operands -> instructions -------------------------------------------------------
    Parsed<std::vector<Instruction>> build(const Statement& st, u32 pc) const {
        const std::vector<Operand> ops = st.operands();
        const std::string_view mn = st.mnemonic;

        if (const Pseudo* p = find_pseudo(mn, ops.size())) {
            if (ops.size() != p->count) {
                return error_at(st.column, "{} expects {} operand{} ({}), got {}", mn, p->count,
                                p->count == 1 ? "" : "s", p->syntax, ops.size());
            }
            return build_pseudo(mn, ops, pc);
        }

        const Op op = *isa::find_op(mn);
        const Form form = isa::info(op).form;
        const FormSyntax syntax = form_syntax(form);
        if (ops.size() != syntax.count) {
            return error_at(st.column, "{} expects {} operand{} ({}), got {}", mn, syntax.count,
                            syntax.count == 1 ? "" : "s", syntax.text, ops.size());
        }
        const ImmKind kind = isa::imm_kind_of(form);
        Instruction in{op};
        // Every helper returns Parsed<T>; TRY unwraps it or returns its error.
#define TRY(var, expr)                                         \
    auto var##_r = (expr);                                     \
    if (!var##_r) return std::unexpected(var##_r.error());     \
    const auto var = *var##_r

        switch (form) {
            case Form::RegRegReg: {
                TRY(rd, reg(ops[0])); TRY(rs1, reg(ops[1])); TRY(rs2, reg(ops[2]));
                in.rd = rd; in.rs1 = rs1; in.rs2 = rs2;
                break;
            }
            case Form::RegRegImm: case Form::RegRegUimm: case Form::RegRegShamt: case Form::Jalr: {
                TRY(rd, reg(ops[0])); TRY(rs1, reg(ops[1])); TRY(imm, immediate(ops[2], kind, mn));
                in.rd = rd; in.rs1 = rs1; in.imm = imm;
                break;
            }
            case Form::RegUimm: {
                TRY(rd, reg(ops[0])); TRY(imm, immediate(ops[1], kind, mn));
                in.rd = rd; in.imm = imm;
                break;
            }
            case Form::Load: {
                TRY(rd, reg(ops[0])); TRY(m, memory(ops[1], mn));
                in.rd = rd; in.rs1 = m.base; in.imm = m.offset;
                break;
            }
            case Form::Store: {
                TRY(rs2, reg(ops[0])); TRY(m, memory(ops[1], mn));
                in.rs2 = rs2; in.rs1 = m.base; in.imm = m.offset;
                break;
            }
            case Form::Branch: {
                TRY(rs1, reg(ops[0])); TRY(rs2, reg(ops[1])); TRY(t, target(ops[2], pc, kind, mn));
                in.rs1 = rs1; in.rs2 = rs2; in.imm = t;
                break;
            }
            case Form::Jal: {
                TRY(rd, reg(ops[0])); TRY(t, target(ops[1], pc, kind, mn));
                in.rd = rd; in.imm = t;
                break;
            }
            case Form::Swap: {
                TRY(rd, reg(ops[0])); TRY(rs2, reg(ops[1])); TRY(m, memory(ops[2], mn));
                if (m.offset != 0) {
                    return error_at(ops[2].column, "SWAP takes a plain address like (a0), with no offset");
                }
                in.rd = rd; in.rs2 = rs2; in.rs1 = m.base;
                break;
            }
            case Form::NoOperands:
                break;
            case Form::TlbFlush: {
                TRY(rs1, reg(ops[0])); TRY(rs2, reg(ops[1]));
                in.rs1 = rs1; in.rs2 = rs2;
                break;
            }
            case Form::CsrReg: {
                TRY(rd, reg(ops[0])); TRY(c, csr(ops[1])); TRY(rs1, reg(ops[2]));
                in.rd = rd; in.imm = c; in.rs1 = rs1;
                break;
            }
            case Form::CsrImm: {
                TRY(rd, reg(ops[0])); TRY(c, csr(ops[1])); TRY(u5, uimm5(ops[2], mn));
                in.rd = rd; in.imm = c; in.rs1 = static_cast<u8>(u5);
                break;
            }
        }
        return std::vector<Instruction>{in};
    }

    Parsed<std::vector<Instruction>> build_pseudo(std::string_view mn,
                                                  const std::vector<Operand>& ops, u32 pc) const {
        using V = std::vector<Instruction>;
        const auto I = [](Op op, u8 rd, u8 rs1, u8 rs2, i32 imm) {
            return Instruction{op, rd, rs1, rs2, imm};
        };
        const u8 zero = isa::reg::kZero;
        const u8 ra = isa::reg::kRa;

        if (mn == "NOP") return V{I(Op::Addi, zero, zero, zero, 0)};
        if (mn == "RET") return V{I(Op::Jalr, zero, ra, zero, 0)};
        if (mn == "LI") {
            TRY(rd, reg(ops[0])); TRY(v, li_value(ops[1]));
            return expand_li(rd, v);
        }
        if (mn == "LA") {
            TRY(rd, reg(ops[0]));
            const Parsed<i64> v = eval(ops[1], true);
            if (!v) return std::unexpected(v.error());
            if (*v < kMin32 || *v > kMax32) {
                return error_at(ops[1].column, "LA: value {} does not fit in 32 bits", *v);
            }
            const auto x = static_cast<u32>(*v);
            return V{I(Op::Lui, rd, zero, zero, static_cast<i32>(x >> 16)),
                     I(Op::Ori, rd, rd, zero, static_cast<i32>(x & 0xFFFF))};
        }
        if (mn == "MOV" || mn == "NOT" || mn == "NEG" || mn == "SEQZ" || mn == "SNEZ") {
            TRY(rd, reg(ops[0])); TRY(rs, reg(ops[1]));
            if (mn == "MOV") return V{I(Op::Addi, rd, rs, zero, 0)};
            if (mn == "NOT") return V{I(Op::Nor, rd, rs, zero, 0)};
            if (mn == "NEG") return V{I(Op::Sub, rd, zero, rs, 0)};
            if (mn == "SEQZ") return V{I(Op::Sltiu, rd, rs, zero, 1)};
            return V{I(Op::Sltu, rd, zero, rs, 0)};
        }
        if (mn == "BEQZ" || mn == "BNEZ") {
            const Op op = mn == "BEQZ" ? Op::Beq : Op::Bne;
            TRY(rs, reg(ops[0])); TRY(t, target(ops[1], pc, ImmKind::SplitSigned16, mn));
            return V{I(op, zero, rs, zero, t)};
        }
        if (mn == "BGT" || mn == "BLE" || mn == "BGTU" || mn == "BLEU") {
            // a > b  is  b < a: swap the operands (spec §11.2).
            const Op op = mn == "BGT" ? Op::Blt : mn == "BLE" ? Op::Bge : mn == "BGTU" ? Op::Bltu : Op::Bgeu;
            TRY(a, reg(ops[0])); TRY(b, reg(ops[1])); TRY(t, target(ops[2], pc, ImmKind::SplitSigned16, mn));
            return V{I(op, zero, b, a, t)};
        }
        if (mn == "J" || mn == "JAL" || mn == "CALL") {
            TRY(t, target(ops[0], pc, ImmKind::Signed21, mn));
            return V{I(Op::Jal, mn == "J" ? zero : ra, zero, zero, t)};
        }
        if (mn == "JR") {
            TRY(rs, reg(ops[0]));
            return V{I(Op::Jalr, zero, rs, zero, 0)};
        }
        if (mn == "CSRR") {
            TRY(rd, reg(ops[0])); TRY(c, csr(ops[1]));
            return V{I(Op::Csrrs, rd, zero, zero, c)};
        }
        if (mn == "CSRW") {
            TRY(c, csr(ops[0])); TRY(rs, reg(ops[1]));
            return V{I(Op::Csrrw, zero, rs, zero, c)};
        }
#undef TRY
        ASTERIA_ASSERT(false, "pseudo-instruction without an expansion");
        return V{};
    }

    void put(Section s, u32 offset, u32 value, unsigned width) {
        std::vector<u8>& out = bytes_[index(s)];
        ASTERIA_ASSERT(offset + width <= out.size(), "pass 2 wrote past the pass-1 size");
        for (unsigned k = 0; k < width; ++k) out[offset + k] = static_cast<u8>(value >> (8 * k));
    }

    void pass2() {
        for (const Statement& st : statements_) {
            const u32 pc = static_cast<u32>(base_[index(st.section)] + st.offset);
            switch (st.kind) {
                case StmtKind::Instruction: {
                    const Parsed<std::vector<Instruction>> code = build(st, pc);
                    if (!code) {
                        report(st.line, code.error());
                        break;
                    }
                    ASTERIA_ASSERT(code->size() * 4 == st.size, "instruction size changed in pass 2");
                    u32 at = st.offset;
                    for (const Instruction& in : *code) {
                        const Result<u32> word = isa::encode(in);
                        if (!word) {
                            report(st.line, st.column, word.error().message);
                            break;
                        }
                        put(st.section, at, *word, 4);
                        at += 4;
                    }
                    break;
                }
                case StmtKind::Data: {
                    if (st.data_width == 0) {  // .ascii / .asciz
                        std::ranges::copy(st.bytes, bytes_[index(st.section)].begin() + st.offset);
                        break;
                    }
                    const i64 lo = st.data_width == 4 ? kMin32 : -(i64{1} << (8 * st.data_width - 1));
                    const i64 hi = st.data_width == 4 ? kMax32 : (i64{1} << (8 * st.data_width)) - 1;
                    u32 at = st.offset;
                    for (const Operand& op : st.operands()) {
                        const Parsed<i64> v = eval(op, true);
                        if (!v) {
                            report(st.line, v.error());
                        } else if (*v < lo || *v > hi) {
                            report(st.line, op.column,
                                   std::format("value {} does not fit in {} byte{}", *v,
                                               st.data_width, st.data_width == 1 ? "" : "s"));
                        } else {
                            put(st.section, at, static_cast<u32>(*v), st.data_width);
                        }
                        at += st.data_width;
                    }
                    break;
                }
                case StmtKind::Fill:
                    break;  // zeros, already there
            }
        }
    }

    // ---- final checks and the executable --------------------------------------------------------
    void finish() {
        for (const Request& g : globals_) {
            const auto it = symbols_.find(g.name);
            if (it == symbols_.end() || !it->second.is_label) {
                report(g.line, g.column, std::format("'.global {}': no label with that name", g.name));
            } else if (g.name.starts_with(".L")) {
                report(g.line, g.column, std::format("'.global {}': local labels can't be global", g.name));
            }
        }
        if (size_[index(Section::Text)] == 0) {
            report(1, 1, "the program has no instructions (.text is empty)");
            return;
        }
        exe_.entry = static_cast<u32>(base_[index(Section::Text)]);
        if (entry_) {
            const auto it = symbols_.find(entry_->name);
            if (it == symbols_.end() || !it->second.is_label || it->second.section != Section::Text ||
                it->second.offset >= size_[index(Section::Text)] || it->second.offset % 4 != 0) {
                report(entry_->line, entry_->column,
                       std::format("'.entry {}': not a label on an instruction in .text", entry_->name));
            } else {
                exe_.entry = address_of(it->second);
            }
        }
        if (!errors_.empty()) return;

        for (std::size_t s = 0; s < kSectionCount; ++s) {
            if (size_[s] == 0) continue;
            axf::Segment seg;
            seg.vaddr = static_cast<u32>(base_[s]);
            seg.mem_size = size_[s];
            seg.flags = kSectionFlags[s];
            if (static_cast<Section>(s) != Section::Bss) seg.data = std::move(bytes_[s]);
            exe_.segments.push_back(std::move(seg));
        }
        for (const auto& [sym, def] : symbols_) {
            if (!def.is_label || sym.starts_with(".L")) continue;
            const bool global = std::ranges::any_of(globals_, [&](const Request& g) { return g.name == sym; });
            exe_.symbols.push_back({sym, address_of(def), global});
        }
        axf::sort_symbols(exe_.symbols);
        const Result<void> ok = axf::validate(exe_);
        ASTERIA_ASSERT(ok.has_value(), "the assembler built an invalid executable");
    }

    std::string_view source_;
    std::string_view file_;
    struct Located {
        unsigned line;
        unsigned column;
        std::string text;
    };
    std::vector<Located> errors_;

    Section section_ = Section::Text;
    std::array<u32, kSectionCount> size_{};
    std::array<u64, kSectionCount> base_{};
    std::array<std::vector<u8>, kSectionCount> bytes_;
    std::vector<Statement> statements_;
    std::map<std::string, SymbolDef, std::less<>> symbols_;
    std::vector<Request> globals_;
    std::optional<Request> entry_;
    axf::Executable exe_;
};

}  // namespace

Result<axf::Executable> assemble(std::string_view source, std::string_view file_name) {
    // Source is ASCII or UTF-8. Editors sometimes add a UTF-8 byte-order mark: skip it.
    // UTF-16 (what Windows PowerShell's '>' writes) would lex as garbage, so say what it is.
    if (source.starts_with("\xEF\xBB\xBF")) {
        source.remove_prefix(3);
    } else if (source.starts_with("\xFF\xFE") || source.starts_with("\xFE\xFF")) {
        return fail("{}:1:1: error: the file is UTF-16 encoded; save it as UTF-8 or ASCII",
                    file_name);
    }
    return Assembler{source, file_name}.run();
}

}  // namespace asteria::toolchain::as
