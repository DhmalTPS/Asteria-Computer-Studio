#pragma once
// The two-level page walk (spec §8.4) and the permission check (§8.5), as one pure function.
//
// walk() only READS: it returns the physical address, or the page fault / bus error, and it
// says whether the leaf PTE needs its A/D bits set. The caller writes that PTE back only
// after it knows the whole access will succeed, which keeps the trap precise (D2).
//
// The M4 hardware walker (mem/mmu) will produce the same answers with timing and a TLB; the
// differential tests compare it against this function.

#include <optional>

#include "common/types.hpp"
#include "cpu/core/trap.hpp"
#include "sim/functional_memory.hpp"

namespace asteria::cpu::interp {

// PTE fields (spec §8.3).
namespace pte {
inline constexpr u32 kV = 1u << 0;
inline constexpr u32 kR = 1u << 1;
inline constexpr u32 kW = 1u << 2;
inline constexpr u32 kX = 1u << 3;
inline constexpr u32 kU = 1u << 4;
inline constexpr u32 kG = 1u << 5;
inline constexpr u32 kA = 1u << 6;
inline constexpr u32 kD = 1u << 7;
inline constexpr unsigned kPpnLo = 12;  // [31:12]
}  // namespace pte

struct WalkResult {
    std::optional<core::Trap> trap;  // set = the access faults; everything below is unused
    u32 paddr = 0;
    std::optional<u32> pte_address;  // set = write `updated_pte` back here before the access
    u32 updated_pte = 0;
};

// `access` is Fetch, Load, Store or Swap. SWAP needs R and W and sets A and D (§4.4a).
[[nodiscard]] WalkResult walk(sim::FunctionalMemory& memory, u32 ptbr, u32 vaddr,
                              sim::Access access, bool user_mode);

// The exception an access type raises for a page fault or a bus error (§5.2).
[[nodiscard]] isa::Exception page_fault_for(sim::Access access);
[[nodiscard]] isa::Exception bus_error_for(sim::Access access);

}  // namespace asteria::cpu::interp
