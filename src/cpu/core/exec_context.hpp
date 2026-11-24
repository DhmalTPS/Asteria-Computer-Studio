#pragma once
// ExecContext: everything an instruction's semantics may touch (D23).
//
// The semantics layer (semantics.hpp) describes what each instruction DOES, once. Each CPU
// model describes HOW: where registers live, how a virtual address becomes a physical one,
// how long memory takes. That split is this interface.
//
// Memory contract. The semantics layer checks alignment first (spec §4.4: before
// translation), so every address passed here is aligned to its size. An implementation must
// then be PRECISE (D2): when it reports a trap, nothing has changed - no memory, no
// page-table A/D bit. When it succeeds, the access has fully happened.

#include <optional>

#include "common/types.hpp"
#include "cpu/core/csr_file.hpp"
#include "cpu/core/register_file.hpp"
#include "cpu/core/trap.hpp"

namespace asteria::cpu::core {

// The outcome of a load or a SWAP: the value read (zero-extended), or a trap.
struct LoadResult {
    u32 value = 0;
    std::optional<Trap> trap;
};

class ExecContext {
public:
    virtual ~ExecContext() = default;

    [[nodiscard]] virtual RegisterFile& regs() = 0;
    [[nodiscard]] virtual CsrFile& csrs() = 0;

    // Data accesses at a VIRTUAL address, `size` = 1, 2 or 4.
    [[nodiscard]] virtual LoadResult load(u32 vaddr, unsigned size) = 0;
    [[nodiscard]] virtual std::optional<Trap> store(u32 vaddr, unsigned size, u32 value) = 0;
    // Indivisible word read-then-write (spec §4.4a). Faults like a store.
    [[nodiscard]] virtual LoadResult swap(u32 vaddr, u32 value) = 0;

    // FENCE.I (§9): later fetches see earlier stores.
    virtual void fence_i() = 0;
    // TLBFLUSH (§8.6): nullopt = all addresses / all ASIDs including global entries.
    virtual void tlb_flush(std::optional<u32> vaddr, std::optional<u8> asid) = 0;
};

}  // namespace asteria::cpu::core
