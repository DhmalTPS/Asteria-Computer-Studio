#pragma once
// FunctionalMemory: the contract between an UNTIMED memory user and physical memory.
//
// The full memory-port protocol (D7: request IDs, accept/refuse, responses after N cycles)
// arrives with the timed CPUs in M3. The reference interpreter (M1) has no notion of time:
// an access either happens now or fails now. This interface is that untimed subset, and it
// keeps the same layering: the CPU sees only this contract, never a concrete RAM, ROM or bus.
//
// Two kinds of access:
//   read/write          what the simulated machine does. Every rule of the memory map
//                       applies (ROM is read-only, MMIO is word-only and never fetched, ...).
//   debug_read/write    what the HOST does: the loader, a debugger, a test. Ignores access
//                       rules (it can write ROM), has no side effects, and fails only on
//                       addresses where nothing exists (D7).
//
// accepts() answers "would this access succeed?" without doing it. The interpreter needs it
// to stay precise (D2): it must know an access will succeed before it commits anything else,
// such as setting a page-table entry's Accessed bit.

#include <optional>
#include <span>

#include "common/types.hpp"

namespace asteria::sim {

// Who is asking, and why. Region rules depend on it (docs/platform/MEMORY-MAP.md section 1).
enum class Access : u8 {
    Fetch,      // instruction fetch
    Load,       // LB/LH/LW/LBU/LHU
    Store,      // SB/SH/SW
    Swap,       // SWAP: one indivisible read + write (spec §4.4a)
    WalkRead,   // the page-table walker reading a PTE
    WalkWrite,  // the walker writing a PTE back with A/D set
};

class FunctionalMemory {
public:
    virtual ~FunctionalMemory() = default;

    // Would an access of `size` bytes (1, 2 or 4) at `paddr` succeed? No side effects.
    // `paddr` must be aligned to `size`: alignment is checked by the CPU before it gets here.
    [[nodiscard]] virtual bool accepts(u32 paddr, unsigned size, Access access) const = 0;

    // The value read (zero-extended, little-endian), or nullopt for a bus error.
    [[nodiscard]] virtual std::optional<u32> read(u32 paddr, unsigned size, Access access) = 0;

    // Writes the low `size` bytes of `value`. false = bus error, and nothing was written.
    [[nodiscard]] virtual bool write(u32 paddr, unsigned size, u32 value, Access access) = 0;

    // Host access. false = some byte of the range is unpopulated; then nothing is copied.
    [[nodiscard]] virtual bool debug_read(u32 paddr, std::span<u8> out) const = 0;
    [[nodiscard]] virtual bool debug_write(u32 paddr, std::span<const u8> in) = 0;
};

}  // namespace asteria::sim
