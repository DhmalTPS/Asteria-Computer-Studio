#include "toolchain/as/lexer.hpp"

#include <format>
#include <optional>

#include "common/ascii.hpp"

namespace asteria::toolchain::as {

namespace {

bool is_ident_start(char c) { return ascii::is_alpha(c) || c == '_' || c == '.' || c == '$'; }
bool is_ident_char(char c) { return is_ident_start(c) || ascii::is_digit(c); }

std::optional<unsigned> digit_value(char c, unsigned base) {
    unsigned v = 0;
    if (ascii::is_digit(c)) v = static_cast<unsigned>(c - '0');
    else if (c >= 'a' && c <= 'f') v = static_cast<unsigned>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v = static_cast<unsigned>(c - 'A' + 10);
    else return std::nullopt;
    return v < base ? std::optional<unsigned>{v} : std::nullopt;
}

class Lexer {
public:
    explicit Lexer(std::string_view line) : s_(line) {}

    std::expected<std::vector<Token>, LexError> run() {
        std::vector<Token> tokens;
        while (true) {
            while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\r')) ++i_;
            if (i_ >= s_.size() || s_[i_] == ';' || s_[i_] == '#') break;

            const std::size_t start = i_;
            const char c = s_[i_];
            Token t{TokenKind::Comma, {}, column(start)};
            if (is_ident_start(c)) {
                while (i_ < s_.size() && is_ident_char(s_[i_])) ++i_;
                t.kind = TokenKind::Identifier;
            } else if (ascii::is_digit(c)) {
                if (auto err = number(t)) return std::unexpected(*err);
            } else if (c == '\'') {
                if (auto err = character(t)) return std::unexpected(*err);
            } else if (c == '"') {
                if (auto err = string(t)) return std::unexpected(*err);
            } else {
                ++i_;
                switch (c) {
                    case ',': t.kind = TokenKind::Comma; break;
                    case '(': t.kind = TokenKind::LParen; break;
                    case ')': t.kind = TokenKind::RParen; break;
                    case ':': t.kind = TokenKind::Colon; break;
                    case '+': t.kind = TokenKind::Plus; break;
                    case '-': t.kind = TokenKind::Minus; break;
                    case '%': t.kind = TokenKind::Percent; break;
                    default:
                        return std::unexpected(LexError{
                            column(start), std::format("unexpected character '{}'", c)});
                }
            }
            t.text = s_.substr(start, i_ - start);
            tokens.push_back(std::move(t));
        }
        return tokens;
    }

private:
    unsigned column(std::size_t at) const { return static_cast<unsigned>(at + 1); }

    std::optional<LexError> number(Token& t) {
        const std::size_t start = i_;
        unsigned base = 10;
        if (s_[i_] == '0' && i_ + 1 < s_.size() && (s_[i_ + 1] == 'x' || s_[i_ + 1] == 'X')) {
            base = 16;
            i_ += 2;
        } else if (s_[i_] == '0' && i_ + 1 < s_.size() && (s_[i_ + 1] == 'b' || s_[i_ + 1] == 'B')) {
            base = 2;
            i_ += 2;
        }
        u64 value = 0;
        bool any_digit = false;
        while (i_ < s_.size() && (is_ident_char(s_[i_]))) {
            const char c = s_[i_];
            if (c == '_') {
                ++i_;
                continue;
            }
            const std::optional<unsigned> d = digit_value(c, base);
            if (!d) {
                return LexError{column(i_), std::format("'{}' is not a digit in base {}", c, base)};
            }
            if (value > (0xFFFF'FFFF'FFFFull - *d) / base) {
                return LexError{column(start), "number is too large"};
            }
            value = value * base + *d;
            any_digit = true;
            ++i_;
        }
        if (!any_digit) {
            return LexError{column(start), "number has no digits"};
        }
        t.kind = TokenKind::Number;
        t.number = value;
        return std::nullopt;
    }

    // One possibly-escaped character inside quotes. nullopt + error on a bad escape.
    std::optional<LexError> escaped(char quote, u8& out) {
        if (i_ >= s_.size()) {
            return LexError{column(i_), std::format("missing closing {}", quote)};
        }
        const char c = s_[i_++];
        if (c != '\\') {
            out = static_cast<u8>(c);
            return std::nullopt;
        }
        if (i_ >= s_.size()) {
            return LexError{column(i_), "unfinished escape sequence"};
        }
        const char e = s_[i_++];
        switch (e) {
            case 'n': out = '\n'; return std::nullopt;
            case 't': out = '\t'; return std::nullopt;
            case 'r': out = '\r'; return std::nullopt;
            case '0': out = 0; return std::nullopt;
            case '\\': out = '\\'; return std::nullopt;
            case '\'': out = '\''; return std::nullopt;
            case '"': out = '"'; return std::nullopt;
            case 'x': {
                unsigned v = 0;
                for (int k = 0; k < 2; ++k) {
                    const std::optional<unsigned> d =
                        i_ < s_.size() ? digit_value(s_[i_], 16) : std::nullopt;
                    if (!d) return LexError{column(i_), "\\x needs exactly two hex digits"};
                    v = v * 16 + *d;
                    ++i_;
                }
                out = static_cast<u8>(v);
                return std::nullopt;
            }
            default:
                return LexError{column(i_ - 2), std::format("unknown escape sequence '\\{}'", e)};
        }
    }

    std::optional<LexError> character(Token& t) {
        const std::size_t start = i_++;
        if (i_ < s_.size() && s_[i_] == '\'') {
            return LexError{column(start), "empty character literal"};
        }
        u8 value = 0;
        if (auto err = escaped('\'', value)) return err;
        if (i_ >= s_.size() || s_[i_] != '\'') {
            return LexError{column(start), "character literal must be one character"};
        }
        ++i_;
        t.kind = TokenKind::Number;
        t.number = value;
        return std::nullopt;
    }

    std::optional<LexError> string(Token& t) {
        const std::size_t start = i_++;
        while (true) {
            if (i_ >= s_.size()) {
                return LexError{column(start), "missing closing \""};
            }
            if (s_[i_] == '"') {
                ++i_;
                break;
            }
            u8 c = 0;
            if (auto err = escaped('"', c)) return err;
            t.string.push_back(static_cast<char>(c));
        }
        t.kind = TokenKind::String;
        return std::nullopt;
    }

    std::string_view s_;
    std::size_t i_ = 0;
};

}  // namespace

std::expected<std::vector<Token>, LexError> tokenize_line(std::string_view line) {
    return Lexer{line}.run();
}

}  // namespace asteria::toolchain::as
