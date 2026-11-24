#pragma once
// The reference interpreter: ASTERIA-32 executed one instruction at a time, with no timing.
//
// It is the ORACLE. Every other CPU model is correct exactly when it ends in the same
// architectural state as this one (spec §10). So it is written for clarity, never speed:
// fetch, translate, decode, execute, and nothing microarchitectural (no TLB, no caches).
//
// One call to step() does exactly one of these:
//   - takes a pending interrupt (at an instruction boundary, §5.3)       -> Trapped
//   - executes one instruction that completes                             -> Retired
//   - executes one instruction that raises an exception (no effect, D2)   -> Trapped
//   - executes HALT                                                       -> Halted
//   - executes WFI with nothing pending                                   -> Waiting
// Each step counts one CYCLE. INSTRET counts completed instructions (Retired and Halted).
//
// Paging (§8) uses a functional page walk on every access: there is no TLB to go stale, so
// TLBFLUSH and FENCE.I are no-ops here. Page tables must live in ROM or RAM.

#include <optional>

#include "common/types.hpp"
#include "cpu/core/csr_file.hpp"
#include "cpu/core/exec_context.hpp"
#include "cpu/core/register_file.hpp"
#include "cpu/core/trap.hpp"
#include "sim/functional_memory.hpp"

namespace asteria::cpu::interp {

enum class StepKind : u8 { Retired, Trapped, Halted, Waiting };

struct StepResult {
    StepKind kind = StepKind::Retired;
    u32 pc = 0;    // address of the instruction this step was about (or the interrupted PC)
    u32 word = 0;  // the fetched instruction word; 0 if nothing was fetched
    std::optional<core::Trap> trap;  // for Trapped
};

class Interpreter final : private core::ExecContext {
public:
    explicit Interpreter(sim::FunctionalMemory& memory) : memory_(memory) { reset(); }

    // Spec §1: registers 0, PC 0 (the Boot ROM), Kernel mode, interrupts and paging off.
    // Memory belongs to the bus and is not touched.
    void reset();

    StepResult step();

    // The 8 interrupt lines as seen in IPEND (§5.3). Level-sensitive: a line stays pending
    // until the device lowers it.
    void set_interrupt_lines(u8 lines) noexcept { csrs_.set_interrupt_lines(lines); }

    [[nodiscard]] u32 pc() const noexcept { return pc_; }
    void set_pc(u32 pc);  // used by the host loader; must be a multiple of 4

    [[nodiscard]] core::RegisterFile& registers() noexcept { return regs_; }
    [[nodiscard]] const core::RegisterFile& registers() const noexcept { return regs_; }
    [[nodiscard]] core::CsrFile& csr_file() noexcept { return csrs_; }
    [[nodiscard]] const core::CsrFile& csr_file() const noexcept { return csrs_; }

    [[nodiscard]] bool halted() const noexcept { return halted_; }
    [[nodiscard]] bool waiting() const noexcept { return waiting_; }

private:
    // ---- core::ExecContext ----------------------------------------------------------------
    core::RegisterFile& regs() override { return regs_; }
    core::CsrFile& csrs() override { return csrs_; }
    core::LoadResult load(u32 vaddr, unsigned size) override;
    std::optional<core::Trap> store(u32 vaddr, unsigned size, u32 value) override;
    core::LoadResult swap(u32 vaddr, u32 value) override;
    void fence_i() override {}
    void tlb_flush(std::optional<u32>, std::optional<u8>) override {}

    // Translates and checks one access, and on success performs the page-table A/D update.
    // Returns the physical address, or the trap; on a trap nothing has changed.
    struct Checked {
        u32 paddr = 0;
        std::optional<core::Trap> trap;
    };
    Checked prepare(u32 vaddr, unsigned size, sim::Access access);

    StepResult step_once();
    StepResult trap_step(u32 pc, u32 word, const core::Trap& trap);

    sim::FunctionalMemory& memory_;
    core::RegisterFile regs_;
    core::CsrFile csrs_;
    u32 pc_ = 0;
    bool halted_ = false;
    bool waiting_ = false;  // a WFI at pc_ is stalled
    u32 waiting_word_ = 0;  // that WFI's instruction word, for StepResult
};

}  // namespace asteria::cpu::interp
