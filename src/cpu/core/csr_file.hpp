#pragma once
// The CSR state (spec §7) and the two operations that change privilege: trap entry (§5.1)
// and ERET (§4.7). Shared by every CPU model (D23).
//
// What the CSRs mean is described in isa/csrs.hpp (numbers, masks, access rights). This
// class HOLDS them. A write keeps only the register's writable bits, so undefined bits
// always read as 0 (§7).
//
// Live values are not stored as CSRs:
//   IPEND               mirrors the interrupt lines, which the CPU model sets each step
//   CYCLE, INSTRET      64-bit counters, read as two 32-bit halves

#include <array>
#include <optional>

#include "common/types.hpp"
#include "cpu/core/trap.hpp"
#include "isa/csrs.hpp"
#include "isa/traps.hpp"

namespace asteria::cpu::core {

class CsrFile {
public:
    // Spec §1 and §7: Kernel mode, interrupts off, paging off, counters zero.
    void reset() noexcept { *this = CsrFile{}; }

    // ---- CSR instructions (§4.8) -------------------------------------------------------
    // Checks an access before anything happens, so a refused instruction has no effect.
    // `writes` = the instruction would write the CSR (CSRRS/CSRRC with rs1 = R0 do not).
    // Order follows the exception priority (§5.2): IllegalInstruction (unknown CSR, or a
    // write to a read-only one) before PrivilegeViolation (a K-only CSR in User mode).
    [[nodiscard]] std::optional<isa::Exception> check_access(u16 number, bool writes) const;

    // Both assert that check_access() allowed the access.
    [[nodiscard]] u32 read(u16 number) const;
    void write(u16 number, u32 value);  // keeps only the writable bits

    // ---- Fields the CPU reads directly -------------------------------------------------
    [[nodiscard]] bool kernel_mode() const noexcept { return (status_ & isa::status::kKm) != 0; }
    [[nodiscard]] bool interrupts_enabled() const noexcept {
        return (status_ & isa::status::kIe) != 0;
    }
    [[nodiscard]] u32 status() const noexcept { return status_; }
    [[nodiscard]] u32 ptbr() const noexcept { return ptbr_; }
    [[nodiscard]] u32 imask() const noexcept { return imask_; }
    [[nodiscard]] u32 ipend() const noexcept { return ipend_; }

    // Interrupt lines that are pending AND enabled in IMASK (ignores STATUS.IE).
    [[nodiscard]] u32 pending_enabled() const noexcept { return ipend_ & imask_; }

    // ---- Inputs from the CPU model -----------------------------------------------------
    void set_interrupt_lines(u8 lines) noexcept { ipend_ = lines; }
    void count_cycle() noexcept { ++cycle_; }
    void count_retired() noexcept { ++instret_; }
    [[nodiscard]] u64 cycles() const noexcept { return cycle_; }
    [[nodiscard]] u64 retired() const noexcept { return instret_; }

    // ---- Privilege changes ---------------------------------------------------------------
    // Trap entry (§5.1). `epc` is the PC of the faulting instruction, or of the next
    // unexecuted one for an interrupt. Returns the trap target (§5.4).
    [[nodiscard]] u32 enter_trap(const Trap& trap, u32 epc);

    // ERET (§4.7): IE <- PIE, KM <- PKM. PIE and PKM keep their values. Returns EPC.
    [[nodiscard]] u32 eret();

    friend bool operator==(const CsrFile&, const CsrFile&) = default;

private:
    // Reset values (§7). Only STATUS is non-zero: KM = 1 (Kernel mode). A unit test checks
    // these against the reset column of isa::kCsrs.
    u32 status_ = isa::status::kKm;
    u32 cause_ = 0;
    u32 epc_ = 0;
    u32 badvaddr_ = 0;
    u32 tvec_ = 0;
    u32 scratch_ = 0;
    u32 ptbr_ = 0;
    u32 imask_ = 0;
    u32 ipend_ = 0;
    u64 cycle_ = 0;
    u64 instret_ = 0;
};

}  // namespace asteria::cpu::core
