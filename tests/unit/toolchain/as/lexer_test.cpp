#include <catch2/catch_test_macros.hpp>

#include "toolchain/as/lexer.hpp"

using namespace asteria;
using namespace asteria::toolchain::as;

TEST_CASE("Lexer: tokens, columns and comments") {
    const auto t = tokenize_line("loop: ADDI a0, a0, -1   ; count down");
    REQUIRE(t.has_value());
    REQUIRE(t->size() == 9);
    CHECK((*t)[0].kind == TokenKind::Identifier);
    CHECK((*t)[0].text == "loop");
    CHECK((*t)[1].kind == TokenKind::Colon);
    CHECK((*t)[2].text == "ADDI");
    CHECK((*t)[2].column == 7);
    CHECK((*t)[7].kind == TokenKind::Minus);
    CHECK((*t)[8].number == 1);

    CHECK(tokenize_line("  # only a comment").value().empty());
    CHECK(tokenize_line("").value().empty());
    CHECK(tokenize_line(".Lx $y _z.w").value().size() == 3);
}

TEST_CASE("Lexer: numbers in every base, with separators") {
    const auto n = [](std::string_view s) { return tokenize_line(s).value().at(0).number; };
    CHECK(n("42") == 42);
    CHECK(n("0x8000_0000") == 0x8000'0000);
    CHECK(n("0XfF") == 255);
    CHECK(n("0b1010") == 10);
    CHECK(n("'A'") == 65);
    CHECK(n("'\\n'") == 10);
    CHECK(n("'\\x7F'") == 127);
    CHECK(n("0") == 0);

    const auto err = [](std::string_view s) { return tokenize_line(s).error().message; };
    CHECK(err("0x") == "number has no digits");
    CHECK(err("12a") == "'a' is not a digit in base 10");
    CHECK(err("0b102") == "'2' is not a digit in base 2");
    CHECK(err("0x1_0000_0000_0000_0") == "number is too large");
    CHECK(err("''") == "empty character literal");
    CHECK(err("'ab'") == "character literal must be one character");
}

TEST_CASE("Lexer: strings and escapes") {
    const auto t = tokenize_line(R"(.asciz "a\tb\"c\\\0")");
    REQUIRE(t.has_value());
    REQUIRE(t->size() == 2);
    CHECK((*t)[1].kind == TokenKind::String);
    CHECK((*t)[1].string == std::string("a\tb\"c\\\0", 7));
    CHECK(tokenize_line(R"("; not a comment")").value().at(0).string == "; not a comment");

    const auto err = tokenize_line(R"(.ascii "abc)");
    REQUIRE_FALSE(err.has_value());
    CHECK(err.error().message == "missing closing \"");
    CHECK(tokenize_line(R"("\q")").error().message == "unknown escape sequence '\\q'");
    CHECK(tokenize_line(R"("\x4")").error().message == "\\x needs exactly two hex digits");
}
