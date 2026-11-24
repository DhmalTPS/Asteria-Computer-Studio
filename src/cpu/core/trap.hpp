#pragma once
// Trap: what a CPU records when an exception or interrupt happens (spec §5).
//
// A trap is DATA, not a C++ error (CODING-STYLE "Errors"): a page fault is the machine
// working correctly. The semantics layer returns a Trap; the CPU then performs trap entry
// (CsrFile::enter_trap).
//
// `cause` is the exact value that trap entry writes to the CAUSE CSR:
//     bit 31 = interrupt, [15:8] = page-fault subcode, [7:0] = exception code or IRQ number.

#include <optional>

#include "common/assert.hpp"
#include "common/types.hpp"
#include "isa/csrs.hpp"
#include "isa/traps.hpp"

namespace asteria::cpu::core {

struct Trap {
    u32 cause = 0;
    std::optional<u32> badvaddr;  // set for address exceptions only (§5.2); else unchanged

    // An exception that does not report an address (IllegalInstruction, ECALL, ...).
    [[nodiscard]] static Trap exception(isa::Exception e) {
        ASTERIA_ASSERT(!isa::sets_badvaddr(e), "this exception needs a faulting address");
        return Trap{static_cast<u32>(e), std::nullopt};
    }

    // An address exception: misaligned access, bus error, page fault.
    [[nodiscard]] static Trap at(isa::Exception e, u32 address) {
        ASTERIA_ASSERT(isa::sets_badvaddr(e), "this exception does not report an address");
        return Trap{static_cast<u32>(e), address};
    }

    [[nodiscard]] static Trap page_fault(isa::Exception e, isa::PageFaultReason reason,
                                         u32 address) {
        ASTERIA_ASSERT(isa::is_page_fault(e), "not a page-fault exception");
        const u32 subcode = static_cast<u32>(reason) << isa::cause::kSubcodeLo;
        return Trap{static_cast<u32>(e) | subcode, address};
    }

    [[nodiscard]] static Trap interrupt(unsigned irq) {
        ASTERIA_ASSERT(irq < isa::kIrqCount, "IRQ number out of range");
        return Trap{isa::cause::kInterrupt | irq, std::nullopt};
    }

    [[nodiscard]] bool is_interrupt() const noexcept {
        return (cause & isa::cause::kInterrupt) != 0;
    }
    [[nodiscard]] u32 code() const noexcept { return cause & 0xFFu; }  // CAUSE[7:0]

    friend bool operator==(const Trap&, const Trap&) = default;
};

}  // namespace asteria::cpu::core
