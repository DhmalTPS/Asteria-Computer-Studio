#include <catch2/catch_test_macros.hpp>

#include <string>

#include "toolchain/as/assembler.hpp"
#include "toolchain/as/listing.hpp"

using namespace asteria;
using namespace asteria::toolchain;

namespace {

// assemble -> list -> assemble must give back exactly the same executable.
void round_trip(std::string_view source) {
    const Result<axf::Executable> first = as::assemble(source, "a.asm");
    if (!first) FAIL(first.error().message);
    const Result<std::string> text = as::listing(*first);
    if (!text) FAIL(text.error().message);
    const Result<axf::Executable> second = as::assemble(*text, "listing.asm");
    if (!second) FAIL(second.error().message + "\n--- listing ---\n" + *text);
    CHECK(*second == *first);
    CHECK(axf::write(*second).value() == axf::write(*first).value());
}

}  // namespace

TEST_CASE("Listing: round trip of a program with every section") {
    round_trip(R"(
        .global main
        .entry  main
    helper: RET
    main:   LA   a0, msg
            CALL helper
            BEQ  a0, zero, done
            .word 0             ; not an instruction: listed as .word
    done:   HALT
        .rodata
    msg:    .asciz "hello, world"
    msg_end:
        .data
            .space 3
    odd:    .byte 1, 2, 3
        .bss
    buf:    .space 100
    buf_end:
    )");
}

TEST_CASE("Listing: round trip of labels at section edges") {
    round_trip("HALT\nend_of_text:");                        // label after the last instruction
    round_trip("HALT\n.data\nfirst: .word 1\nlast:");        // label at the end of .data
    round_trip("HALT\n.bss\na:\nb: .space 8\nc:");           // several labels at one address
    round_trip("e: HALT\n.rodata\n.byte 1\n.bss\n.space 1"); // unlabelled sections
    round_trip(".entry x\nNOP\n.Lx: x: HALT");               // entry at a later instruction
}

TEST_CASE("Listing: entry without a symbol gets a local label") {
    const axf::Executable exe = as::assemble(".entry .Lgo\nNOP\n.Lgo: HALT", "a.asm").value();
    const std::string text = as::listing(exe).value();
    CHECK(text.find(".entry .Lentry") != std::string::npos);
    CHECK(as::assemble(text, "l.asm").value() == exe);
}

TEST_CASE("Listing: refuses executables the assembler could not have produced") {
    axf::Executable exe = as::assemble("HALT", "a.asm").value();
    exe.segments[0].vaddr = 0x8000'1000;
    exe.entry = 0x8000'1000;
    CHECK_FALSE(as::listing(exe).has_value());

    exe = as::assemble("HALT", "a.asm").value();
    exe.symbols.push_back({"far", 0x9000'0000, false});
    CHECK(as::listing(exe).error().message ==
          "symbol 'far' (0x90000000) is not at an address inside any section");

    exe = as::assemble("HALT", "a.asm").value();
    exe.symbols.push_back({"R5", 0x8000'0000, false});
    CHECK(as::listing(exe).error().message == "symbol 'R5' can't be written as a label");
}
