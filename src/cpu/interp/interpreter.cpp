#include "cpu/interp/interpreter.hpp"

#include <bit>

#include "common/assert.hpp"
#include "common/bits.hpp"
#include "cpu/core/semantics.hpp"
#include "cpu/interp/page_walk.hpp"
#include "isa/csrs.hpp"
#include "isa/encoding.hpp"

namespace asteria::cpu::interp {

using core::Outcome;
using core::Trap;
using sim::Access;

void Interpreter::reset() {
    regs_.reset();
    csrs_.reset();
    pc_ = 0;  // the Boot ROM (spec §1)
    halted_ = false;
    waiting_ = false;
    waiting_word_ = 0;
}

void Interpreter::set_pc(u32 pc) {
    ASTERIA_ASSERT(bits::is_aligned(pc, 4), "PC must be a multiple of 4");
    pc_ = pc;
}

Interpreter::Checked Interpreter::prepare(u32 vaddr, unsigned size, Access access) {
    // 1. Translate (paging on) or use the address as is (paging off, §8.1).
    u32 paddr = vaddr;
    std::optional<u32> pte_address;
    u32 updated_pte = 0;
    if (csrs_.ptbr() & isa::ptbr::kEnable) {
        const WalkResult w = walk(memory_, csrs_.ptbr(), vaddr, access, !csrs_.kernel_mode());
        if (w.trap) {
            return {0, w.trap};
        }
        paddr = w.paddr;
        pte_address = w.pte_address;
        updated_pte = w.updated_pte;
    }

    // 2. Would the bus accept the access? Checked BEFORE the A/D write-back, so a failing
    //    access leaves the page table untouched.
    if (!memory_.accepts(paddr, size, access)) {
        return {0, Trap::at(bus_error_for(access), vaddr)};
    }

    // 3. Set A (and D for stores) in the leaf PTE. If the table can't be written (ROM), the
    //    access fails with a bus error of its own type (§8.4), and still nothing has changed.
    if (pte_address && !memory_.write(*pte_address, 4, updated_pte, Access::WalkWrite)) {
        return {0, Trap::at(bus_error_for(access), vaddr)};
    }
    return {paddr, std::nullopt};
}

core::LoadResult Interpreter::load(u32 vaddr, unsigned size) {
    const Checked c = prepare(vaddr, size, Access::Load);
    if (c.trap) {
        return {0, c.trap};
    }
    const std::optional<u32> value = memory_.read(c.paddr, size, Access::Load);
    ASTERIA_ASSERT(value.has_value(), "bus refused an access it had accepted");
    return {*value, std::nullopt};
}

std::optional<Trap> Interpreter::store(u32 vaddr, unsigned size, u32 value) {
    const Checked c = prepare(vaddr, size, Access::Store);
    if (c.trap) {
        return c.trap;
    }
    const bool written = memory_.write(c.paddr, size, value, Access::Store);
    ASTERIA_ASSERT(written, "bus refused an access it had accepted");
    return std::nullopt;
}

core::LoadResult Interpreter::swap(u32 vaddr, u32 value) {
    // The interpreter runs nothing in between, so read-then-write is indivisible (§4.4a).
    const Checked c = prepare(vaddr, 4, Access::Swap);
    if (c.trap) {
        return {0, c.trap};
    }
    const std::optional<u32> old = memory_.read(c.paddr, 4, Access::Swap);
    const bool written = memory_.write(c.paddr, 4, value, Access::Swap);
    ASTERIA_ASSERT(old.has_value() && written, "bus refused an access it had accepted");
    return {*old, std::nullopt};
}

StepResult Interpreter::trap_step(u32 pc, u32 word, const Trap& trap) {
    pc_ = csrs_.enter_trap(trap, pc);
    return {StepKind::Trapped, pc, word, trap};
}

StepResult Interpreter::step() {
    if (halted_) {
        return {StepKind::Halted, pc_, 0, std::nullopt};  // stays halted; no state changes
    }
    const StepResult result = step_once();
    csrs_.count_cycle();  // after the step, so a CSR read of CYCLE sees the count before it
    return result;
}

StepResult Interpreter::step_once() {
    const u32 pc = pc_;
    ASTERIA_ASSERT(bits::is_aligned(pc, 4), "PC is always a multiple of 4");

    // A stalled WFI completes as soon as a line is pending and enabled, regardless of IE
    // (§4.7). An enabled interrupt is then taken at the NEXT boundary, so its EPC points
    // after the WFI and the handler's ERET does not land back in the wait.
    if (waiting_) {
        if (csrs_.pending_enabled() == 0) {
            return {StepKind::Waiting, pc, waiting_word_, std::nullopt};
        }
        waiting_ = false;
        pc_ = pc + 4;
        csrs_.count_retired();
        return {StepKind::Retired, pc, waiting_word_, std::nullopt};
    }

    // Interrupts are taken at instruction boundaries (§5.3); the lowest line wins.
    if (csrs_.interrupts_enabled() && csrs_.pending_enabled() != 0) {
        const auto irq = static_cast<unsigned>(std::countr_zero(csrs_.pending_enabled()));
        return trap_step(pc, 0, Trap::interrupt(irq));  // EPC = the next unexecuted instruction
    }

    // Fetch.
    const Checked fetch = prepare(pc, 4, Access::Fetch);
    if (fetch.trap) {
        return trap_step(pc, 0, *fetch.trap);
    }
    const std::optional<u32> word = memory_.read(fetch.paddr, 4, Access::Fetch);
    ASTERIA_ASSERT(word.has_value(), "bus refused a fetch it had accepted");

    // Decode. A word that is not an instruction is IllegalInstruction (§2, D3).
    const std::optional<isa::Instruction> decoded = isa::decode(*word);
    if (!decoded) {
        return trap_step(pc, *word, Trap::exception(isa::Exception::IllegalInstruction));
    }

    // Execute.
    const Outcome out = core::execute(*decoded, pc, *this);
    switch (out.kind) {
        case Outcome::Kind::Next:
            pc_ = out.next_pc;
            csrs_.count_retired();
            return {StepKind::Retired, pc, *word, std::nullopt};
        case Outcome::Kind::Trap:
            return trap_step(pc, *word, out.trap);
        case Outcome::Kind::Halt:
            halted_ = true;  // PC stays at the HALT, which counts as completed (§4.7)
            csrs_.count_retired();
            return {StepKind::Halted, pc, *word, std::nullopt};
        case Outcome::Kind::Wait:
            waiting_ = true;
            waiting_word_ = *word;
            return {StepKind::Waiting, pc, *word, std::nullopt};
    }
    ASTERIA_ASSERT(false, "unhandled outcome");
    return {};
}

}  // namespace asteria::cpu::interp
