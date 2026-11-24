#include "cpu/interp/page_walk.hpp"

#include "common/assert.hpp"
#include "common/bits.hpp"
#include "isa/csrs.hpp"

namespace asteria::cpu::interp {

using isa::Exception;
using isa::PageFaultReason;
using sim::Access;

isa::Exception page_fault_for(Access access) {
    switch (access) {
        case Access::Fetch: return Exception::InstrPageFault;
        case Access::Load: return Exception::LoadPageFault;
        case Access::Store: case Access::Swap: return Exception::StorePageFault;
        default: ASTERIA_ASSERT(false, "not a CPU access"); return Exception::LoadPageFault;
    }
}

isa::Exception bus_error_for(Access access) {
    switch (access) {
        case Access::Fetch: return Exception::InstrBusError;
        case Access::Load: return Exception::LoadBusError;
        case Access::Store: case Access::Swap: return Exception::StoreBusError;
        default: ASTERIA_ASSERT(false, "not a CPU access"); return Exception::LoadBusError;
    }
}

namespace {

// §8.5: what the leaf must allow for this access.
bool permitted(u32 entry, Access access, bool user_mode) {
    if (user_mode && !(entry & pte::kU)) {
        return false;  // User mode may touch only U pages. Kernel mode may touch all pages.
    }
    switch (access) {
        case Access::Fetch: return (entry & pte::kX) != 0;
        case Access::Load: return (entry & pte::kR) != 0;
        case Access::Store: return (entry & pte::kW) != 0;
        case Access::Swap: return (entry & pte::kR) && (entry & pte::kW);
        default: return false;
    }
}

}  // namespace

WalkResult walk(sim::FunctionalMemory& memory, u32 ptbr, u32 vaddr, Access access,
                bool user_mode) {
    const auto page_fault = [&](PageFaultReason reason) {
        return WalkResult{core::Trap::page_fault(page_fault_for(access), reason, vaddr)};
    };
    const bool is_store = access == Access::Store || access == Access::Swap;
    const u32 vpn[2] = {bits::extract(vaddr, 21, 12), bits::extract(vaddr, 31, 22)};

    // 1. Start at the root table.
    u32 table = ptbr & ~bits::mask(isa::ptbr::kPpnLo);
    for (int level = 1; level >= 0; --level) {
        // 2. Read the PTE (a physical read). Tables outside ROM/RAM are a bus error.
        const u32 entry_address = table + vpn[level] * 4;
        const std::optional<u32> read = memory.read(entry_address, 4, Access::WalkRead);
        if (!read) {
            return WalkResult{core::Trap::at(bus_error_for(access), vaddr)};
        }
        const u32 entry = *read;

        // 3. Invalid, or the reserved W-without-R combination.
        if (!(entry & pte::kV)) {
            return page_fault(PageFaultReason::NotPresent);
        }
        if ((entry & pte::kW) && !(entry & pte::kR)) {
            return page_fault(PageFaultReason::PermissionDenied);
        }

        const u32 ppn = entry >> pte::kPpnLo;
        // 4. A pointer (R = W = X = 0) leads one level down; at level 0 there is nowhere to go.
        if (!(entry & (pte::kR | pte::kW | pte::kX))) {
            if (level == 0) {
                return page_fault(PageFaultReason::NotPresent);
            }
            table = ppn << pte::kPpnLo;
            continue;
        }

        // 5. A leaf at level 1 maps a 4 MiB superpage: its PPN must be 4 MiB-aligned.
        if (level == 1 && bits::extract(ppn, 9, 0) != 0) {
            return page_fault(PageFaultReason::MisalignedSuperpage);
        }
        // 6. Permissions.
        if (!permitted(entry, access, user_mode)) {
            return page_fault(PageFaultReason::PermissionDenied);
        }

        // 8. The physical address.
        WalkResult result;
        result.paddr = level == 1 ? (ppn << pte::kPpnLo) | bits::extract(vaddr, 21, 0)
                                  : (ppn << pte::kPpnLo) | bits::extract(vaddr, 11, 0);

        // 7. Accessed / Dirty. Reported, not written: the caller writes back only once it
        //    knows the access itself will succeed.
        const u32 updated = entry | pte::kA | (is_store ? pte::kD : 0u);
        if (updated != entry) {
            result.pte_address = entry_address;
            result.updated_pte = updated;
        }
        return result;
    }
    ASTERIA_ASSERT(false, "page walk fell through");
    return {};
}

}  // namespace asteria::cpu::interp
