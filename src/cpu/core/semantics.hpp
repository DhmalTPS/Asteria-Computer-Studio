#pragma once
// Instruction semantics: the effect of every ASTERIA-32 instruction, written ONCE (D23).
//
// execute() is the spec's section 4 turned into code. The reference interpreter, the
// single-cycle CPU and the multi-cycle CPU all call it, so they cannot disagree about what
// an instruction does. They can only differ in timing, which is what Asteria measures.
//
// Precise traps (D2): if execute() returns a trap, it has changed NOTHING. Every check that
// can fail (privilege, CSR access, alignment, the memory access itself) happens before the
// first write to a register, a CSR or memory.

#include "common/types.hpp"
#include "cpu/core/exec_context.hpp"
#include "cpu/core/trap.hpp"
#include "isa/encoding.hpp"

namespace asteria::cpu::core {

struct Outcome {
    enum class Kind : u8 {
        Next,  // completed; continue at next_pc
        Trap,  // raised `trap`; no architectural effect
        Halt,  // HALT completed; the machine stops with PC still at the HALT
        Wait,  // WFI found nothing pending; it has not completed and will be retried
    };

    Kind kind = Kind::Next;
    u32 next_pc = 0;  // for Next
    Trap trap;        // for Trap

    [[nodiscard]] static Outcome next(u32 pc) { return {Kind::Next, pc, {}}; }
    [[nodiscard]] static Outcome raise(Trap t) { return {Kind::Trap, 0, t}; }
    [[nodiscard]] static Outcome halt() { return {Kind::Halt, 0, {}}; }
    [[nodiscard]] static Outcome wait() { return {Kind::Wait, 0, {}}; }
};

// Executes one decoded instruction located at `pc`.
[[nodiscard]] Outcome execute(const isa::Instruction& in, u32 pc, ExecContext& ctx);

}  // namespace asteria::cpu::core
