#include "cpu/core/csr_file.hpp"

#include "common/assert.hpp"

namespace asteria::cpu::core {

using isa::Csr;

namespace {

isa::CsrInfo info_for(u16 number) {
    const std::optional<isa::CsrInfo> info = isa::find_csr(number);
    ASTERIA_ASSERT(info.has_value(), "CSR access was not checked first");
    return *info;
}

u32 low(u64 v) { return static_cast<u32>(v); }
u32 high(u64 v) { return static_cast<u32>(v >> 32); }

}  // namespace

std::optional<isa::Exception> CsrFile::check_access(u16 number, bool writes) const {
    const std::optional<isa::CsrInfo> info = isa::find_csr(number);
    if (!info || (writes && info->read_only)) {
        return isa::Exception::IllegalInstruction;
    }
    if (info->kernel_only && !kernel_mode()) {
        return isa::Exception::PrivilegeViolation;
    }
    return std::nullopt;
}

u32 CsrFile::read(u16 number) const {
    switch (info_for(number).csr) {
        case Csr::Status: return status_;
        case Csr::Cause: return cause_;
        case Csr::Epc: return epc_;
        case Csr::BadVaddr: return badvaddr_;
        case Csr::Tvec: return tvec_;
        case Csr::Scratch: return scratch_;
        case Csr::Ptbr: return ptbr_;
        case Csr::Imask: return imask_;
        case Csr::Ipend: return ipend_;
        case Csr::Cycle: return low(cycle_);
        case Csr::CycleH: return high(cycle_);
        case Csr::Instret: return low(instret_);
        case Csr::InstretH: return high(instret_);
        case Csr::CpuId: return isa::kCpuIdValue;
    }
    ASTERIA_ASSERT(false, "unhandled CSR");
    return 0;
}

void CsrFile::write(u16 number, u32 value) {
    const isa::CsrInfo info = info_for(number);
    ASTERIA_ASSERT(!info.read_only, "write to a read-only CSR was not checked first");
    const u32 kept = value & info.writable_mask;  // undefined bits read as 0 (§7)
    switch (info.csr) {
        case Csr::Status: status_ = kept; return;
        case Csr::Cause: cause_ = kept; return;
        case Csr::Epc: epc_ = kept; return;
        case Csr::BadVaddr: badvaddr_ = kept; return;
        case Csr::Tvec: tvec_ = kept; return;
        case Csr::Scratch: scratch_ = kept; return;
        case Csr::Ptbr: ptbr_ = kept; return;
        case Csr::Imask: imask_ = kept; return;
        default: ASTERIA_ASSERT(false, "unhandled writable CSR"); return;
    }
}

u32 CsrFile::enter_trap(const Trap& trap, u32 epc) {
    using namespace isa::status;
    // §5.1: save IE and KM into PIE and PKM, then disable interrupts and enter Kernel mode.
    u32 s = status_ & ~(kPie | kPkm | kIe);
    if (status_ & kIe) s |= kPie;
    if (status_ & kKm) s |= kPkm;
    status_ = s | kKm;

    // Through write(), so the same writable masks apply as for a CSR instruction.
    write(static_cast<u16>(Csr::Epc), epc);
    write(static_cast<u16>(Csr::Cause), trap.cause);
    if (trap.badvaddr) {
        write(static_cast<u16>(Csr::BadVaddr), *trap.badvaddr);
    }

    // §5.4: direct mode sends every trap to base; vectored mode sends IRQ i to base + 4(1 + i).
    const u32 base = tvec_ & isa::tvec::kBaseMask;
    if ((tvec_ & isa::tvec::kVectored) && trap.is_interrupt()) {
        return base + 4 * (1 + trap.code());
    }
    return base;
}

u32 CsrFile::eret() {
    using namespace isa::status;
    u32 s = status_ & ~(kIe | kKm);
    if (status_ & kPie) s |= kIe;
    if (status_ & kPkm) s |= kKm;
    status_ = s;
    return epc_;
}

}  // namespace asteria::cpu::core
