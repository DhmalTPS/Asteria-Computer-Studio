#include <catch2/catch_test_macros.hpp>

#include "test_machine.hpp"

using namespace asteria;
using namespace asteria::test;
using cpu::interp::StepKind;
using isa::Exception;
namespace st = asteria::isa::status;
using C = isa::Csr;

TEST_CASE("interpreter: straight-line ALU code, then HALT with the exit code in a0") {
    TestMachine m;
    m.load(kRam, {
        I(Op::Addi, 1, 0, 0, 10),          // R1 = 10
        I(Op::Addi, 2, 0, 0, -3),          // R2 = -3
        I(Op::Add, 3, 1, 2),               // R3 = 7
        I(Op::Mul, 5, 1, 2),               // R5 = -30
        I(Op::Lui, 6, 0, 0, 0x8001),       // R6 = 0x8001_0000
        I(Op::Ori, 6, 6, 0, 0xFFFF),       // R6 = 0x8001_FFFF
        I(Op::Auipc, 7, 0, 0, 1),          // R7 = PC (0x8000_0018) + 0x1_0000
        I(Op::Addi, 4, 0, 0, 0),           // a0 = 0
        I(Op::Halt),
    });
    m.run_to_halt();

    CHECK(m.reg(3) == 7u);
    CHECK(m.reg(5) == static_cast<u32>(-30));
    CHECK(m.reg(6) == 0x8001'FFFFu);
    CHECK(m.reg(7) == 0x8001'0018u);
    CHECK(m.cpu.pc() == kRam + 8 * 4);  // PC stays at the HALT
    CHECK(m.cpu.halted());
    CHECK(m.csr(C::Instret) == 9u);  // HALT counts as completed
    CHECK(m.csr(C::Cycle) == 9u);

    // A halted machine stays halted and changes nothing.
    CHECK(m.cpu.step().kind == StepKind::Halted);
    CHECK(m.csr(C::Cycle) == 9u);
}

TEST_CASE("interpreter: R0 stays zero, even as a load destination") {
    TestMachine m;
    m.poke(kRam + 0x100, 0xDEAD'BEEF);
    m.load(kRam, {
        I(Op::Addi, 0, 0, 0, 5),
        I(Op::Lui, 1, 0, 0, 0x8000),
        I(Op::Lw, 0, 1, 0, 0x100),
        I(Op::Halt),
    });
    m.run_to_halt();
    CHECK(m.reg(0) == 0u);
}

TEST_CASE("interpreter: loads extend by size and sign; stores write little-endian") {
    TestMachine m;
    m.poke(kRam + 0x100, 0x80FF'7F01);
    m.load(kRam, {
        I(Op::Lui, 1, 0, 0, 0x8000),       // R1 = RAM base
        I(Op::Lb, 2, 1, 0, 0x102),         // 0xFF -> -1
        I(Op::Lbu, 3, 1, 0, 0x102),        // 0xFF -> 255
        I(Op::Lh, 5, 1, 0, 0x102),         // 0x80FF -> sign-extended
        I(Op::Lhu, 6, 1, 0, 0x102),
        I(Op::Lw, 7, 1, 0, 0x100),
        I(Op::Sh, 0, 1, 7, 0x200),         // low half of R7 at +0x200
        I(Op::Sb, 0, 1, 2, 0x203),         // 0xFF at +0x203
        I(Op::Sw, 0, 1, 7, -4 + 0x210),    // negative offsets work too: +0x20C
        I(Op::Halt),
    });
    m.run_to_halt();
    CHECK(m.reg(2) == 0xFFFF'FFFFu);
    CHECK(m.reg(3) == 0xFFu);
    CHECK(m.reg(5) == 0xFFFF'80FFu);
    CHECK(m.reg(6) == 0x0000'80FFu);
    CHECK(m.reg(7) == 0x80FF'7F01u);
    CHECK(m.peek(kRam + 0x200) == 0xFF00'7F01u);
    CHECK(m.peek(kRam + 0x20C) == 0x80FF'7F01u);
}

TEST_CASE("interpreter: branches, JAL and JALR (rd may equal rs1)") {
    TestMachine m;
    m.load(kRam, {
        I(Op::Addi, 1, 0, 0, 5),           // 0x00  counter
        I(Op::Addi, 2, 2, 0, 3),           // 0x04  loop: R2 += 3
        I(Op::Addi, 1, 1, 0, -1),          // 0x08  R1 -= 1
        I(Op::Bne, 0, 1, 0, -2),           // 0x0C  back to 0x04 while R1 != 0
        I(Op::Jal, 31, 0, 0, 3),           // 0x10  call 0x1C, R31 = 0x14
        I(Op::Addi, 4, 0, 0, 0),           // 0x14  a0 = 0
        I(Op::Halt),                       // 0x18
        I(Op::Jalr, 31, 31, 0, 0),         // 0x1C  return; R31 = 0x20 afterwards
    });
    m.run_to_halt();
    CHECK(m.reg(2) == 15u);
    CHECK(m.reg(31) == kRam + 0x20);
    CHECK(m.cpu.pc() == kRam + 0x18);
}

TEST_CASE("interpreter: a misaligned load traps precisely (spec sections 4.4, 5.1)") {
    TestMachine m;
    m.set_csr(C::Tvec, kRam + 0x400);
    m.load(kRam, {
        I(Op::Lui, 1, 0, 0, 0x8000),
        I(Op::Addi, 2, 0, 0, 77),
        I(Op::Lw, 2, 1, 0, 0x102),         // EA % 4 = 2
    });
    for (int i = 0; i < 2; ++i) (void)m.cpu.step();
    const auto r = m.cpu.step();

    CHECK(r.kind == StepKind::Trapped);
    CHECK(r.pc == kRam + 8);
    CHECK(m.reg(2) == 77u);  // unchanged
    CHECK(m.csr(C::Cause) == 7u);
    CHECK(m.csr(C::BadVaddr) == kRam + 0x102);
    CHECK(m.csr(C::Epc) == kRam + 8);
    CHECK(m.cpu.pc() == kRam + 0x400);
    CHECK(m.csr(C::Instret) == 2u);  // the trapping instruction does not count
    CHECK(m.csr(C::Cycle) == 3u);    // but its step does
}

TEST_CASE("interpreter: bus errors for ROM stores, MMIO loads and fetches from nowhere") {
    TestMachine m;
    m.set_csr(C::Tvec, kRam + 0x400);

    SECTION("a store to address 0 hits the Boot ROM: StoreBusError") {
        m.load(kRam, {I(Op::Addi, 3, 0, 0, 1), I(Op::Sw, 0, 0, 3, 0)});
        (void)m.cpu.step();
        CHECK(m.cpu.step().kind == StepKind::Trapped);
        CHECK(m.csr(C::Cause) == 12u);
        CHECK(m.csr(C::BadVaddr) == 0u);
        CHECK(m.peek(0) == 0u);
    }
    SECTION("no devices exist yet, so an MMIO load is a LoadBusError") {
        m.load(kRam, {I(Op::Lui, 1, 0, 0, 0xF000), I(Op::Lw, 2, 1, 0, 0)});
        (void)m.cpu.step();
        (void)m.cpu.step();
        CHECK(m.csr(C::Cause) == 11u);
        CHECK(m.csr(C::BadVaddr) == 0xF000'0000u);
    }
    SECTION("jumping to unpopulated memory faults on the fetch, with EPC = the bad PC") {
        m.load(kRam, {I(Op::Lui, 1, 0, 0, 0x0001), I(Op::Jalr, 0, 1, 0, 0)});
        for (int i = 0; i < 2; ++i) CHECK(m.cpu.step().kind == StepKind::Retired);
        const auto r = m.cpu.step();
        CHECK(r.kind == StepKind::Trapped);
        CHECK(r.word == 0u);
        CHECK(m.csr(C::Cause) == 2u);
        CHECK(m.csr(C::Epc) == 0x0001'0000u);
        CHECK(m.csr(C::BadVaddr) == 0x0001'0000u);
    }
}

TEST_CASE("interpreter: the all-zero word is IllegalInstruction") {
    TestMachine m;
    m.set_csr(C::Tvec, kRam + 0x400);
    const auto r = m.cpu.step();  // RAM is zeroed
    CHECK(r.kind == StepKind::Trapped);
    CHECK(r.trap->cause == 3u);
    CHECK(m.csr(C::Epc) == kRam);
}

TEST_CASE("interpreter: JALR to a misaligned target traps without writing rd") {
    TestMachine m;
    m.set_csr(C::Tvec, kRam + 0x400);
    m.load(kRam, {I(Op::Addi, 1, 0, 0, 0x102), I(Op::Jalr, 1, 1, 0, 0)});
    (void)m.cpu.step();
    (void)m.cpu.step();
    CHECK(m.csr(C::Cause) == 0u);
    CHECK(m.csr(C::BadVaddr) == 0x102u);
    CHECK(m.reg(1) == 0x102u);
}

TEST_CASE("interpreter: ECALL, the handler, and ERET back to User mode") {
    TestMachine m;
    m.set_csr(C::Tvec, kRam + 0x100);
    m.load(kRam, {
        // Kernel: drop to User mode at 0x40 via ERET (PKM = 0, PIE = 0).
        I(Op::Lui, 1, 0, 0, 0x8000),
        I(Op::Addi, 1, 1, 0, 0x40),
        Csr(Op::Csrrw, 0, C::Epc, 1),
        Csr(Op::Csrrci, 0, C::Status, st::kPkm),
        I(Op::Eret),
    });
    m.load(kRam + 0x40, {I(Op::Addi, 5, 0, 0, 1), I(Op::Ecall), I(Op::Addi, 6, 0, 0, 1)});
    m.load(kRam + 0x100, {
        // Handler: skip the ECALL and return.
        Csr(Op::Csrrs, 7, C::Epc, 0),
        I(Op::Addi, 7, 7, 0, 4),
        Csr(Op::Csrrw, 0, C::Epc, 7),
        I(Op::Eret),
    });

    for (int i = 0; i < 5; ++i) REQUIRE(m.cpu.step().kind == StepKind::Retired);
    CHECK_FALSE(m.cpu.csr_file().kernel_mode());
    CHECK(m.cpu.pc() == kRam + 0x40);

    (void)m.cpu.step();                          // ADDI in User mode
    CHECK(m.cpu.step().kind == StepKind::Trapped);  // ECALL
    CHECK(m.csr(C::Cause) == 6u);
    CHECK(m.csr(C::Epc) == kRam + 0x44);
    CHECK(m.cpu.csr_file().kernel_mode());
    CHECK((m.csr(C::Status) & st::kPkm) == 0u);  // came from User mode

    for (int i = 0; i < 4; ++i) REQUIRE(m.cpu.step().kind == StepKind::Retired);
    CHECK_FALSE(m.cpu.csr_file().kernel_mode());
    CHECK(m.cpu.pc() == kRam + 0x48);
    (void)m.cpu.step();
    CHECK(m.reg(6) == 1u);
}

TEST_CASE("interpreter: User mode cannot run privileged instructions or touch K-only CSRs") {
    TestMachine m;
    m.set_csr(C::Tvec, kRam + 0x400);
    m.set_csr(C::Status, 0);  // User mode

    SECTION("HALT") {
        m.load(kRam, {I(Op::Halt)});
        CHECK(m.cpu.step().trap->cause == 4u);
        CHECK_FALSE(m.cpu.halted());
    }
    SECTION("reading EPC") {
        m.load(kRam, {Csr(Op::Csrrs, 1, C::Epc, 0)});
        CHECK(m.cpu.step().trap->cause == 4u);
    }
    SECTION("writing a read-only CSR is IllegalInstruction, which outranks privilege") {
        m.load(kRam, {Csr(Op::Csrrw, 1, C::Ipend, 2)});
        CHECK(m.cpu.step().trap->cause == 3u);
    }
    SECTION("the counters and CPUID are readable") {
        m.load(kRam, {Csr(Op::Csrrs, 1, C::CpuId, 0), Csr(Op::Csrrs, 2, C::Instret, 0)});
        CHECK(m.cpu.step().kind == StepKind::Retired);
        CHECK(m.cpu.step().kind == StepKind::Retired);
        CHECK(m.reg(1) == 0x0001'0000u);
        CHECK(m.reg(2) == 1u);  // sees the count before itself
    }
}

TEST_CASE("interpreter: CSR instructions read and write as spec section 4.8 says") {
    TestMachine m;
    m.set_csr(C::Tvec, kRam + 0x400);
    m.load(kRam, {
        I(Op::Addi, 1, 0, 0, 0x0F0),
        Csr(Op::Csrrw, 2, C::Scratch, 1),   // R2 = 0, SCRATCH = 0xF0
        Csr(Op::Csrrsi, 3, C::Scratch, 5),  // R3 = 0xF0, SCRATCH = 0xF5
        Csr(Op::Csrrc, 5, C::Scratch, 1),   // R5 = 0xF5, SCRATCH = 0x05
        Csr(Op::Csrrwi, 0, C::Imask, 31),   // IMASK = 0x1F
        Csr(Op::Csrrs, 6, C::Cycle, 0),     // read-only, but rs1 = R0: no write, legal
        Csr(Op::Csrrsi, 7, C::Cycle, 0),    // same with uimm5 = 0
        Csr(Op::Csrrwi, 0, C::Cycle, 0),    // CSRRW always writes: IllegalInstruction
    });
    for (int i = 0; i < 7; ++i) REQUIRE(m.cpu.step().kind == StepKind::Retired);
    CHECK(m.reg(2) == 0u);
    CHECK(m.reg(3) == 0xF0u);
    CHECK(m.reg(5) == 0xF5u);
    CHECK(m.csr(C::Scratch) == 0x05u);
    CHECK(m.csr(C::Imask) == 0x1Fu);
    CHECK(m.reg(6) == 5u);  // CYCLE counts the steps before this one
    CHECK(m.reg(7) == 6u);
    CHECK(m.cpu.step().trap->cause == 3u);
}

TEST_CASE("interpreter: SWAP exchanges a word and faults like a store") {
    TestMachine m;
    m.set_csr(C::Tvec, kRam + 0x400);
    m.poke(kRam + 0x200, 111);

    SECTION("exchange, with rd = rs2") {
        m.load(kRam, {
            I(Op::Lui, 1, 0, 0, 0x8000),
            I(Op::Addi, 1, 1, 0, 0x200),
            I(Op::Addi, 2, 0, 0, 222),
            I(Op::Swap, 2, 1, 2),
        });
        for (int i = 0; i < 4; ++i) REQUIRE(m.cpu.step().kind == StepKind::Retired);
        CHECK(m.reg(2) == 111u);
        CHECK(m.peek(kRam + 0x200) == 222u);
    }
    SECTION("misaligned is StoreMisaligned") {
        m.load(kRam, {I(Op::Lui, 1, 0, 0, 0x8000), I(Op::Addi, 1, 1, 0, 0x202), I(Op::Swap, 2, 1, 2)});
        for (int i = 0; i < 3; ++i) (void)m.cpu.step();
        CHECK(m.csr(C::Cause) == 8u);
    }
    SECTION("on ROM it is a StoreBusError, and the ROM is unchanged") {
        m.load(kRam, {I(Op::Addi, 2, 0, 0, 9), I(Op::Swap, 3, 0, 2)});
        (void)m.cpu.step();
        CHECK(m.cpu.step().trap->cause == 12u);
        CHECK(m.reg(3) == 0u);
    }
}

TEST_CASE("interpreter: interrupts are level-sensitive, masked, and lowest-first") {
    TestMachine m;
    m.set_csr(C::Tvec, (kRam + 0x400) | isa::tvec::kVectored);
    m.load(kRam, {I(Op::Addi, 1, 1, 0, 1), I(Op::Addi, 1, 1, 0, 1), I(Op::Addi, 1, 1, 0, 1)});

    m.cpu.set_interrupt_lines(0b1010'0000);  // IRQ 5 and 7
    CHECK(m.cpu.step().kind == StepKind::Retired);  // IE = 0: nothing happens

    m.set_csr(C::Status, st::kKm | st::kIe);
    CHECK(m.cpu.step().kind == StepKind::Retired);  // IMASK = 0: still nothing

    m.set_csr(C::Imask, 0b1110'0000);
    const auto r = m.cpu.step();
    CHECK(r.kind == StepKind::Trapped);
    CHECK(r.trap->cause == 0x8000'0005u);
    CHECK(m.csr(C::Epc) == kRam + 8);  // the next unexecuted instruction
    CHECK(m.cpu.pc() == kRam + 0x400 + 4 * 6);
    CHECK_FALSE(m.cpu.csr_file().interrupts_enabled());
    CHECK(m.reg(1) == 2u);
    CHECK(m.csr(C::Instret) == 2u);
}

TEST_CASE("interpreter: WFI waits, then completes before the interrupt is taken") {
    TestMachine m;
    m.set_csr(C::Tvec, kRam + 0x400);
    m.load(kRam, {I(Op::Wfi), I(Op::Addi, 1, 0, 0, 1)});

    SECTION("nothing pending: the WFI stalls and does not count") {
        for (int i = 0; i < 3; ++i) CHECK(m.cpu.step().kind == StepKind::Waiting);
        CHECK(m.cpu.waiting());
        CHECK(m.cpu.pc() == kRam);
        CHECK(m.csr(C::Instret) == 0u);
        CHECK(m.csr(C::Cycle) == 3u);
    }
    SECTION("a pending, enabled line wakes it even with IE = 0") {
        m.set_csr(C::Imask, 1);
        CHECK(m.cpu.step().kind == StepKind::Waiting);
        m.cpu.set_interrupt_lines(1);
        const auto r = m.cpu.step();
        CHECK(r.kind == StepKind::Retired);
        CHECK(r.word == 0x0060'0030u);  // the WFI
        CHECK(m.cpu.pc() == kRam + 4);
        CHECK(m.cpu.step().kind == StepKind::Retired);  // no interrupt: IE = 0
    }
    SECTION("with IE = 1 the interrupt follows the WFI, so EPC points after it") {
        m.set_csr(C::Imask, 1);
        m.set_csr(C::Status, st::kKm | st::kIe);
        CHECK(m.cpu.step().kind == StepKind::Waiting);
        m.cpu.set_interrupt_lines(1);
        CHECK(m.cpu.step().kind == StepKind::Retired);
        CHECK(m.cpu.step().kind == StepKind::Trapped);
        CHECK(m.csr(C::Epc) == kRam + 4);
    }
}

TEST_CASE("interpreter: reset restores the state of spec section 1") {
    TestMachine m;
    m.load(kRam, {I(Op::Addi, 1, 0, 0, 1), I(Op::Halt)});
    m.run_to_halt();
    m.cpu.reset();
    CHECK(m.cpu.pc() == 0u);
    CHECK(m.reg(1) == 0u);
    CHECK_FALSE(m.cpu.halted());
    CHECK(m.csr(C::Status) == st::kKm);
    CHECK(m.csr(C::Cycle) == 0u);
}
