#pragma once
// A tiny harness for interpreter tests: a PhysicalBus with 1 MiB of RAM, an Interpreter on
// it, and helpers to place encoded instructions and data words in memory.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <initializer_list>
#include <utility>

#include "common/memory_map.hpp"
#include "cpu/interp/interpreter.hpp"
#include "isa/csrs.hpp"
#include "isa/encoding.hpp"
#include "mem/phys/physical_bus.hpp"

namespace asteria::test {

using isa::Instruction;
using isa::Op;

inline constexpr u32 kRam = memory_map::kRamBase;

inline mem::phys::PhysicalBus make_bus() {
    auto bus = mem::phys::PhysicalBus::create({1024 * 1024});
    REQUIRE(bus.has_value());
    return std::move(*bus);
}

// Shorthand for an instruction: I(Op::Addi, rd, rs1, rs2, imm).
inline Instruction I(Op op, u8 rd = 0, u8 rs1 = 0, u8 rs2 = 0, i32 imm = 0) {
    return Instruction{op, rd, rs1, rs2, imm};
}
inline Instruction Csr(Op op, u8 rd, isa::Csr csr, u8 rs1_or_uimm) {
    return Instruction{op, rd, rs1_or_uimm, 0, static_cast<i32>(csr)};
}

struct TestMachine {
    mem::phys::PhysicalBus bus = make_bus();
    cpu::interp::Interpreter cpu{bus};

    TestMachine() { cpu.set_pc(kRam); }

    void poke(u32 address, u32 word) {
        const std::array<u8, 4> bytes{static_cast<u8>(word), static_cast<u8>(word >> 8),
                                      static_cast<u8>(word >> 16), static_cast<u8>(word >> 24)};
        REQUIRE(bus.debug_write(address, bytes));
    }

    [[nodiscard]] u32 peek(u32 address) const {
        std::array<u8, 4> bytes{};
        REQUIRE(bus.debug_read(address, bytes));
        return u32{bytes[0]} | u32{bytes[1]} << 8 | u32{bytes[2]} << 16 | u32{bytes[3]} << 24;
    }

    // Encodes `code` and places it at `address`, one word per instruction.
    void load(u32 address, std::initializer_list<Instruction> code) {
        for (const Instruction& in : code) {
            const Result<u32> word = isa::encode(in);
            REQUIRE(word.has_value());
            poke(address, *word);
            address += 4;
        }
    }

    [[nodiscard]] u32 reg(unsigned n) const { return cpu.registers().read(n); }
    [[nodiscard]] u32 csr(isa::Csr c) const {
        return cpu.csr_file().read(static_cast<u16>(c));
    }
    void set_csr(isa::Csr c, u32 value) { cpu.csr_file().write(static_cast<u16>(c), value); }

    // Steps until HALT; fails the test after `limit` steps.
    void run_to_halt(int limit = 10'000) {
        for (int i = 0; i < limit; ++i) {
            if (cpu.step().kind == cpu::interp::StepKind::Halted) return;
        }
        FAIL("program did not halt");
    }
};

}  // namespace asteria::test
