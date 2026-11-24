#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "isa/encoding.hpp"
#include "isa/registers.hpp"
#include "toolchain/as/assembler.hpp"

using namespace asteria;
using namespace asteria::toolchain;
using asteria::isa::Instruction;
using asteria::isa::Op;

namespace {

constexpr u32 kText = 0x8000'0000;

axf::Executable ok(std::string_view source) {
    Result<axf::Executable> r = as::assemble(source, "t.asm");
    if (!r) FAIL(r.error().message);
    return std::move(*r);
}

std::string error(std::string_view source) {
    const Result<axf::Executable> r = as::assemble(source, "t.asm");
    REQUIRE_FALSE(r.has_value());
    return r.error().message;
}

// The instructions in the text segment (all of it must decode).
std::vector<Instruction> code(std::string_view source) {
    const axf::Executable exe = ok(source);
    const std::vector<u8>& d = exe.segments.at(0).data;
    std::vector<Instruction> out;
    for (std::size_t i = 0; i + 4 <= d.size(); i += 4) {
        const u32 w = u32{d[i]} | u32{d[i + 1]} << 8 | u32{d[i + 2]} << 16 | u32{d[i + 3]} << 24;
        const auto in = isa::decode(w);
        REQUIRE(in.has_value());
        out.push_back(*in);
    }
    return out;
}

Instruction I(Op op, u8 rd, u8 rs1, u8 rs2, i32 imm) { return {op, rd, rs1, rs2, imm}; }

}  // namespace

TEST_CASE("Assembler: real instructions in every operand form") {
    const auto c = code(R"(
        ADD   R3, R1, R2
        addi  a0, zero, -5
        ORI   t0, t0, 0xFFFF
        SLLI  t1, t1, 31
        LUI   v0, 0x1234
        LW    a1, -8(sp)
        SB    a2, (a0)
        BEQ   a0, a1, +2
        JAL   ra, -1
        JALR  zero, ra, 0
        SWAP  v0, a1, (a0)
        CSRRW t0, STATUS, t1
        CSRRSI zero, 0, 31
        TLBFLUSH a0, zero
        HALT
    )");
    REQUIRE(c.size() == 15);
    CHECK(c[0] == I(Op::Add, 3, 1, 2, 0));
    CHECK(c[1] == I(Op::Addi, 4, 0, 0, -5));
    CHECK(c[2].imm == 0xFFFF);
    CHECK(c[3].imm == 31);
    CHECK(c[4] == I(Op::Lui, 2, 0, 0, 0x1234));
    CHECK(c[5] == I(Op::Lw, 5, 29, 0, -8));
    CHECK(c[6] == I(Op::Sb, 0, 4, 6, 0));
    CHECK(c[7] == I(Op::Beq, 0, 4, 5, 2));
    CHECK(c[8] == I(Op::Jal, 31, 0, 0, -1));
    CHECK(c[9] == I(Op::Jalr, 0, 31, 0, 0));
    CHECK(c[10] == I(Op::Swap, 2, 4, 5, 0));
    CHECK(c[11].op == Op::Csrrw);
    CHECK(c[12] == I(Op::Csrrsi, 0, 31, 0, 0));
    CHECK(c[13].op == Op::Tlbflush);
    CHECK(c[14].op == Op::Halt);
}

TEST_CASE("Assembler: labels as targets, forwards and backwards") {
    const auto c = code(R"(
    top:    BEQ  a0, zero, done
            ADDI a0, a0, -1
            J    top
    done:   HALT
    )");
    CHECK(c[0].imm == 3);
    CHECK(c[2] == I(Op::Jal, 0, 0, 0, -2));

    // A number without a sign is an absolute address, not a count.
    CHECK(code("BEQ a0, a0, 0x80000008\nNOP\nHALT")[0].imm == 2);
    CHECK(error("BEQ a0, a0, 0x80000006\nHALT") ==
          "t.asm:1:13: error: BEQ: target 0x80000006 is not a whole number of instructions away");
}

TEST_CASE("Assembler: LI picks the shortest expansion; LA is always two") {
    const auto li = [](std::string_view value) {
        return code(std::string("LI t0, ") + std::string(value) + "\nHALT");
    };
    CHECK(li("-32768").size() == 2);       // ADDI + HALT
    CHECK(li("32767")[0] == I(Op::Addi, 10, 0, 0, 32767));
    CHECK(li("0x10000")[0] == I(Op::Lui, 10, 0, 0, 1));
    CHECK(li("0x10000").size() == 2);      // LUI alone
    const auto big = li("0x12345678");
    REQUIRE(big.size() == 3);
    CHECK(big[0] == I(Op::Lui, 10, 0, 0, 0x1234));
    CHECK(big[1] == I(Op::Ori, 10, 10, 0, 0x5678));
    CHECK(li("-1")[0] == I(Op::Addi, 10, 0, 0, -1));
    CHECK(li("0xFFFFFFFF")[0] == I(Op::Addi, 10, 0, 0, -1));  // same 32-bit value

    CHECK(code(".equ N, 7\nLI a0, N + 1\nHALT")[0].imm == 8);
    CHECK(error("LI a0, N\n.equ N, 7\nHALT") ==
          "t.asm:1:8: error: 'N' is not a constant defined above (.equ); a constant is required "
          "here");
    CHECK(error("x: LI a0, x\nHALT") ==
          "t.asm:1:11: error: 'x' is a label; a constant is required here (use LA for addresses)");
    CHECK(error("LI a0, 0x100000000\nHALT") ==
          "t.asm:1:8: error: LI: value 4294967296 does not fit in 32 bits");

    const auto la = code("LA a0, here\nhere: HALT");
    CHECK(la[0] == I(Op::Lui, 4, 0, 0, 0x8000));
    CHECK(la[1] == I(Op::Ori, 4, 4, 0, 8));
}

TEST_CASE("Assembler: %hi, %lo and %hi_adj") {
    const auto c = code(R"(
        .equ   X, 0x1234_8765
        LUI    t0, %hi(X)
        ORI    t0, t0, %lo(X)
        LUI    t1, %hi_adj(X)
        ADDI   t1, t1, %lo(X)
        HALT
    )");
    CHECK(c[0].imm == 0x1234);
    CHECK(c[1].imm == 0x8765);
    CHECK(c[2].imm == 0x1235);                    // rounded up: the ADDI adds a negative
    CHECK(c[3].imm == static_cast<i16>(0x8765));  // sign-extended in a signed field
    CHECK(error("ADDI t0, t0, %mid(1)\nHALT") ==
          "t.asm:1:15: error: unknown operator '%mid' (use %hi, %lo or %hi_adj)");
}

TEST_CASE("Assembler: pseudo-instructions expand to real ones") {
    const auto c = code(R"(
        NOP
        MOV  a0, a1
        NOT  a0, a1
        NEG  a0, a1
        SEQZ a0, a1
        SNEZ a0, a1
        BGT  a0, a1, +1
        BLEU a0, a1, +1
        BEQZ a0, +1
        CALL +1
        JAL  +1
        JR   t0
        RET
        CSRR a0, STATUS
        CSRW STATUS, a0
        HALT
    )");
    CHECK(c[0] == I(Op::Addi, 0, 0, 0, 0));
    CHECK(c[1] == I(Op::Addi, 4, 5, 0, 0));
    CHECK(c[2] == I(Op::Nor, 4, 5, 0, 0));
    CHECK(c[3] == I(Op::Sub, 4, 0, 5, 0));
    CHECK(c[4] == I(Op::Sltiu, 4, 5, 0, 1));
    CHECK(c[5] == I(Op::Sltu, 4, 0, 5, 0));
    CHECK(c[6] == I(Op::Blt, 0, 5, 4, 1));  // a0 > a1  is  a1 < a0
    CHECK(c[7] == I(Op::Bgeu, 0, 5, 4, 1));
    CHECK(c[8] == I(Op::Beq, 0, 4, 0, 1));
    CHECK(c[9] == I(Op::Jal, 31, 0, 0, 1));
    CHECK(c[10] == I(Op::Jal, 31, 0, 0, 1));
    CHECK(c[11] == I(Op::Jalr, 0, 10, 0, 0));
    CHECK(c[12] == I(Op::Jalr, 0, 31, 0, 0));
    CHECK(c[13].op == Op::Csrrs);
    CHECK(c[13].rs1 == 0);
    CHECK(c[14].op == Op::Csrrw);
    CHECK(c[14].rd == 0);
}

TEST_CASE("Assembler: sections, data directives and layout") {
    const axf::Executable exe = ok(R"(
        .text
        .global main
    main:   HALT
        .rodata
    msg:    .asciz "hi"
            .align 2
    tbl:    .word  1, -1, msg
            .half  0x1234
            .byte  'A', 255, -128
        .data
    var:    .word  7
        .bss
    buf:    .space 4096
    end:
    )");
    REQUIRE(exe.segments.size() == 4);
    CHECK(exe.entry == kText);
    CHECK(exe.segments[0].vaddr == kText);
    CHECK(exe.segments[0].flags == (axf::flag::kRead | axf::flag::kExecute));
    const axf::Segment& ro = exe.segments[1];
    CHECK(ro.vaddr == kText + 0x1000);
    CHECK(ro.flags == axf::flag::kRead);
    CHECK(ro.data == std::vector<u8>{'h', 'i', 0, 0, 1, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF,
                                     0x00, 0x10, 0x00, 0x80, 0x34, 0x12, 'A', 0xFF, 0x80});
    CHECK(exe.segments[2].vaddr == kText + 0x2000);
    CHECK(exe.segments[2].data == std::vector<u8>{7, 0, 0, 0});
    CHECK(exe.segments[3].vaddr == kText + 0x3000);
    CHECK(exe.segments[3].mem_size == 4096);
    CHECK(exe.segments[3].data.empty());

    // Symbols: every non-local label, sorted by (value, name); only main is global.
    REQUIRE(exe.symbols.size() == 6);
    CHECK(exe.symbols[0] == axf::Symbol{"main", kText, true});
    CHECK(exe.symbols[1] == axf::Symbol{"msg", kText + 0x1000, false});
    CHECK(exe.symbols[5] == axf::Symbol{"end", kText + 0x4000, false});

    // Empty sections take no space and make no segment.
    const axf::Executable small = ok(".data\nx: .word 1\n.text\nHALT");
    REQUIRE(small.segments.size() == 2);
    CHECK(small.segments[1].vaddr == kText + 0x1000);
}

TEST_CASE("Assembler: .entry and local labels") {
    const axf::Executable exe = ok(R"(
        .entry start
    helper: RET
    start:  CALL helper
    .Lend:  HALT
    )");
    CHECK(exe.entry == kText + 4);
    REQUIRE(exe.symbols.size() == 2);  // .Lend is not exported
    CHECK(error(".entry v\nHALT\n.data\nv: .word 0") ==
          "t.asm:1:8: error: '.entry v': not a label on an instruction in .text");
}

TEST_CASE("Assembler: errors carry file, line and column, and all are reported") {
    CHECK(error("FOO a0") == "t.asm:1:1: error: unknown instruction 'FOO'");
    CHECK(error("ADD a0, a1") ==
          "t.asm:1:1: error: ADD expects 3 operands (rd, rs1, rs2), got 2");
    CHECK(error("ADDI a0, a0, 40000\nHALT") ==
          "t.asm:1:14: error: ADDI: immediate 40000 out of range -32768..32767");
    CHECK(error("ADD a0, a1, r32\nHALT") ==
          "t.asm:1:13: error: expected a register (R0-R31 or an ABI name), got 'r32'");
    CHECK(error("x: NOP\nx: HALT") == "t.asm:2:1: error: 'x' is already defined on line 1");
    CHECK(error("sp: HALT") == "t.asm:1:1: error: 'sp' is a register name and can't be a label");
    CHECK(error(".data\nNOP") == "t.asm:2:1: error: instructions belong in .text (this is .data)");
    CHECK(error(".bss\n.word 1") ==
          "t.asm:2:1: error: '.word' puts bytes in the file, but .bss has none: use .space");
    CHECK(error(".macro m") ==
          "t.asm:1:1: error: '.macro' is not supported yet: it arrives with the full assembler (M9)");
    CHECK(error(".data\n.byte 1\n.word 2\n.text\nHALT").starts_with(
          "t.asm:3:1: error: '.word' needs a 4-byte-aligned offset"));
    CHECK(error(".byte 1\nHALT") ==
          "t.asm:2:1: error: instruction at offset 1 of .text is not word-aligned; add '.align 2' "
          "before it");
    CHECK(error(".data\n.byte 256\n.text\nHALT") ==
          "t.asm:2:7: error: value 256 does not fit in 1 byte");
    CHECK(error("BEQ a0, a0, nowhere\nHALT") == "t.asm:1:13: error: undefined symbol 'nowhere'");
    CHECK(error(".global nothing\nHALT") ==
          "t.asm:1:9: error: '.global nothing': no label with that name");
    CHECK(error(".data\n.word 1") == "t.asm:1:1: error: the program has no instructions (.text is empty)");
    CHECK(error("ADD a0, a1, @") == "t.asm:1:13: error: unexpected character '@'");

    // Several errors in one build, pass 1 and pass 2 alike, in line order.
    const std::string many = error("FOO\nADDI a0, a0, 99999\nBAR\nJ nowhere");
    CHECK(many.find("t.asm:1:1:") != std::string::npos);
    CHECK(many.find("t.asm:2:14:") != std::string::npos);
    CHECK(many.find("t.asm:3:1:") != std::string::npos);
    CHECK(many.find("t.asm:4:3:") != std::string::npos);
    CHECK(many.find("t.asm:2:14:") < many.find("t.asm:3:1:"));  // line 2 is a pass-2 error
}

TEST_CASE("Assembler: error count is capped") {
    std::string src;
    for (int i = 0; i < 80; ++i) src += "FOO\n";
    const std::string all = error(src);
    CHECK(all.ends_with("\nt.asm: 80 errors in all; only the first 50 are shown"));
    CHECK(all.find("t.asm:50:1:") != std::string::npos);
    CHECK(all.find("t.asm:51:1:") == std::string::npos);
}

TEST_CASE("Assembler: source encodings") {
    CHECK(code("\xEF\xBB\xBFHALT").size() == 1);  // UTF-8 BOM is skipped
    CHECK(code("HALT\r\n").size() == 1);           // CRLF line ends
    CHECK(error(std::string("\xFF\xFEH\0", 4)) ==
          "t.asm:1:1: error: the file is UTF-16 encoded; save it as UTF-8 or ASCII");
}
