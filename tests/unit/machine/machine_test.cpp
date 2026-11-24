#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "isa/registers.hpp"
#include "isa/traps.hpp"
#include "machine/machine.hpp"
#include "toolchain/as/assembler.hpp"

using namespace asteria;
using namespace asteria::machine;
using cpu::core::Trap;
using isa::Exception;

namespace {

constexpr u32 kSmallRam = 1u << 20;  // 1 MiB keeps the tests fast

std::unique_ptr<Machine> machine_with(std::string_view source, u32 ram = kSmallRam) {
    std::unique_ptr<Machine> m = Machine::create({ram}).value();
    const Result<toolchain::axf::Executable> exe = toolchain::as::assemble(source, "t.asm");
    if (!exe) FAIL(exe.error().message);
    REQUIRE(m->load(*exe).has_value());
    return m;
}

}  // namespace

TEST_CASE("Machine: create checks the RAM size") {
    CHECK(Machine::create({kSmallRam}).has_value());
    CHECK_FALSE(Machine::create({100}).has_value());
    CHECK_FALSE(Machine::create({0}).has_value());
}

TEST_CASE("Machine: load sets up PC, stack and memory; run reports the exit code") {
    auto m = machine_with(R"(
        .entry start
        .word 0
    start:  LA   t0, value
            LW   a0, 0(t0)
            HALT
        .data
    value:  .word 42
    )");
    CHECK(m->cpu().pc() == 0x8000'0004);
    CHECK(m->cpu().registers().read(isa::reg::kSp) == 0x8000'0000 + kSmallRam);

    const RunReport r = m->run(100);
    CHECK(r.reason == StopReason::Halted);
    CHECK(r.exit_code == 42);
    CHECK(r.steps == 4);  // LUI, ORI, LW, HALT
    CHECK(r.traps == 0);
    CHECK(m->cpu().halted());
}

TEST_CASE("Machine: a second load starts from a clean slate") {
    auto m = machine_with(".data\nv: .word 7\n.text\nLA t0, v\nLI t1, 9\nSW t1, 0(t0)\nHALT");
    REQUIRE(m->run(100).reason == StopReason::Halted);
    m->cpu().registers().write(5, 123);

    const auto exe =
        toolchain::as::assemble(".data\nv: .word 7\n.text\nLA t0, v\nLW a0, 0(t0)\nHALT", "t.asm");
    REQUIRE(m->load(*exe).has_value());
    CHECK(m->cpu().registers().read(5) == 0);  // registers reset
    const RunReport r = m->run(100);
    CHECK(r.reason == StopReason::Halted);
    CHECK(r.exit_code == 7);                   // the old store was overwritten by the image
}

TEST_CASE("Machine: load refuses programs that don't fit") {
    std::unique_ptr<Machine> m = Machine::create({kSmallRam}).value();
    const auto exe = toolchain::as::assemble("HALT\n.bss\n.space 0x100000", "t.asm").value();
    const Result<void> r = m->load(exe);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message ==
          "segment at 0x80001000 (1048576 bytes) does not fit in RAM (0x80000000-0x800FFFFF)");

    toolchain::axf::Executable bad = exe;
    bad.entry = 0x8000'0002;
    CHECK(m->load(bad).error().message.starts_with("invalid executable: entry point"));
}

TEST_CASE("Machine: run stops at the step limit and can continue") {
    auto m = machine_with("loop: J loop");
    const RunReport r = m->run(1000);
    CHECK(r.reason == StopReason::StepLimit);
    CHECK(r.steps == 1000);
    CHECK(m->run(5).steps == 5);
}

TEST_CASE("Machine: a trap that repeats forever is reported as stuck") {
    // TVEC is 0 after reset: the misaligned load traps to ROM address 0, which holds the zero
    // word, an illegal instruction, which traps to 0 again.
    auto m = machine_with("LI t0, 2\nLW a0, 0(t0)\nHALT");
    const RunReport r = m->run(1000);
    CHECK(r.reason == StopReason::StuckTrap);
    REQUIRE(r.first_trap.has_value());
    CHECK(*r.first_trap == Trap::at(Exception::LoadMisaligned, 2));
    CHECK(r.first_trap_pc == 0x8000'0004);
    REQUIRE(r.last_trap.has_value());
    CHECK(*r.last_trap == Trap::exception(Exception::IllegalInstruction));
    CHECK(r.last_trap_pc == 0);
    CHECK(r.traps == 3);  // the load, then the illegal word twice
}

TEST_CASE("Machine: a handled trap is not a stuck trap") {
    auto m = machine_with(R"(
            LA    t0, handler
            CSRW  TVEC, t0
            ECALL
            ECALL
            HALT
    handler:
            CSRR  t1, EPC
            ADDI  t1, t1, 4
            CSRW  EPC, t1
            ADDI  a0, a0, 1
            ERET
    )");
    const RunReport r = m->run(1000);
    CHECK(r.reason == StopReason::Halted);
    CHECK(r.exit_code == 2);
    CHECK(r.traps == 2);
}

TEST_CASE("Machine: WFI with nothing to wake it stops the run") {
    auto m = machine_with("WFI\nHALT");
    const RunReport r = m->run(1000);
    CHECK(r.reason == StopReason::WaitingForever);
    CHECK(m->cpu().waiting());
}

TEST_CASE("Machine: the observer sees every step") {
    auto m = machine_with("NOP\nNOP\nHALT");
    int seen = 0;
    const RunReport r = m->run(100, [&](const cpu::interp::StepResult&) { ++seen; });
    CHECK(seen == 3);
    CHECK(r.steps == 3);
}
