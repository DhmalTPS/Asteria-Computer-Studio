#pragma once
// ASCII text helpers: case-insensitive comparison for assembly-language names.
//
// The spec makes mnemonics, register names and CSR names case-insensitive (ASTERIA-32 §11.1),
// so both isa/ (name lookups) and toolchain/ (the assembler) compare names this way. The
// helpers are deliberately ASCII-only: assembly source is ASCII, and locale-dependent
// functions like std::toupper would make results depend on the host's settings (D14).

#include <string>
#include <string_view>

namespace asteria::ascii {

[[nodiscard]] constexpr char to_upper(char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

[[nodiscard]] constexpr char to_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// "addi" equals "ADDI" equals "AddI".
[[nodiscard]] constexpr bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (to_upper(a[i]) != to_upper(b[i])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline std::string to_upper(std::string_view text) {
    std::string result{text};
    for (char& c : result) {
        c = to_upper(c);
    }
    return result;
}

[[nodiscard]] constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

[[nodiscard]] constexpr bool is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

}  // namespace asteria::ascii
