#pragma once
// The assembler's lexer: one source line -> tokens (spec §11.1).
//
//   label:  ADDI  a0, zero, 'A'   ; comment        # also a comment
//   ^ident ^colon ^ident ^ident ^comma ^ident ^comma ^number
//
// Numbers: decimal, 0x hex, 0b binary, with optional '_' separators (0x8000_0000), and
// character literals ('A', '\n'). Strings: "text" with the escapes \n \t \r \0 \\ \' \" \xHH.
// A comment (';' or '#') runs to the end of the line, except inside a string or character.

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "common/types.hpp"

namespace asteria::toolchain::as {

enum class TokenKind : u8 {
    Identifier,  // mnemonics, directives (.word), registers, labels, symbols
    Number,      // integer or character literal; value in `number`
    String,      // "..." with escapes resolved into `string`
    Comma,
    LParen,
    RParen,
    Colon,
    Plus,
    Minus,
    Percent,     // %hi( %lo( %hi_adj(
};

struct Token {
    TokenKind kind;
    std::string_view text;  // the exact source text
    unsigned column;        // 1-based
    u64 number = 0;
    std::string string;
};

struct LexError {
    unsigned column;
    std::string message;
};

[[nodiscard]] std::expected<std::vector<Token>, LexError> tokenize_line(std::string_view line);

}  // namespace asteria::toolchain::as
