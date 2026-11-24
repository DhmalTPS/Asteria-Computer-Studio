#pragma once
// Control and status registers (spec §7): numbers, names, access rights, reset values, and
// the bit layout of each register's fields.
//
// This file only DESCRIBES the CSRs. The registers themselves (state, reads, writes, trap
// entry) live in cpu/core/csr_file.hpp, which every CPU model shares.

#include <array>
#include <optional>
#include <string_view>

#include "common/types.hpp"

namespace asteria::isa {

enum class Csr : u16 {
    Status = 0x000,
    Cause = 0x001,
    Epc = 0x002,
    BadVaddr = 0x003,
    Tvec = 0x004,
    Scratch = 0x005,
    Ptbr = 0x006,
    Imask = 0x007,
    Ipend = 0x008,
    Cycle = 0x010,
    CycleH = 0x011,
    Instret = 0x012,
    InstretH = 0x013,
    CpuId = 0x0F0,
};

struct CsrInfo {
    Csr csr;
    std::string_view name;
    bool kernel_only;   // any access from User mode raises PrivilegeViolation
    bool read_only;     // a write raises IllegalInstruction
    u32 reset_value;    // for live values (IPEND, counters) this is the value at reset
    u32 writable_mask;  // bits a write can change; all other bits read as 0 (§7)
};

// Spec §7. Writable masks: STATUS has 4 defined bits; CAUSE keeps [31], [15:8], [7:0];
// EPC bits [1:0] read as 0; TVEC bit [1] reads as 0 (mode is bit [0]); PTBR keeps
// [31:12] PPN, [11:4] ASID and [0] EN; IMASK has one bit per IRQ line.
inline constexpr auto kCsrs = std::to_array<CsrInfo>({
    //  csr            name        kernel ro     reset         writable mask
    {Csr::Status,   "STATUS",   true,  false, 0x0000'0002, 0x0000'000F},
    {Csr::Cause,    "CAUSE",    true,  false, 0x0000'0000, 0x8000'FFFF},
    {Csr::Epc,      "EPC",      true,  false, 0x0000'0000, 0xFFFF'FFFC},
    {Csr::BadVaddr, "BADVADDR", true,  false, 0x0000'0000, 0xFFFF'FFFF},
    {Csr::Tvec,     "TVEC",     true,  false, 0x0000'0000, 0xFFFF'FFFD},
    {Csr::Scratch,  "SCRATCH",  true,  false, 0x0000'0000, 0xFFFF'FFFF},
    {Csr::Ptbr,     "PTBR",     true,  false, 0x0000'0000, 0xFFFF'FFF1},
    {Csr::Imask,    "IMASK",    true,  false, 0x0000'0000, 0x0000'00FF},
    {Csr::Ipend,    "IPEND",    true,  true,  0x0000'0000, 0x0000'0000},
    {Csr::Cycle,    "CYCLE",    false, true,  0x0000'0000, 0x0000'0000},
    {Csr::CycleH,   "CYCLEH",   false, true,  0x0000'0000, 0x0000'0000},
    {Csr::Instret,  "INSTRET",  false, true,  0x0000'0000, 0x0000'0000},
    {Csr::InstretH, "INSTRETH", false, true,  0x0000'0000, 0x0000'0000},
    {Csr::CpuId,    "CPUID",    false, true,  0x0001'0000, 0x0000'0000},
});

// Looks up a CSR by number or by name (case-insensitive). nullopt = no such CSR, which makes
// a CSR instruction raise IllegalInstruction.
[[nodiscard]] std::optional<CsrInfo> find_csr(u16 number) noexcept;
[[nodiscard]] std::optional<CsrInfo> find_csr(std::string_view name) noexcept;

// STATUS fields (spec §7).
namespace status {
inline constexpr u32 kIe = 1u << 0;   // interrupts enabled
inline constexpr u32 kKm = 1u << 1;   // 1 = Kernel mode, 0 = User mode
inline constexpr u32 kPie = 1u << 2;  // IE before the last trap
inline constexpr u32 kPkm = 1u << 3;  // KM before the last trap
}  // namespace status

// CAUSE fields (spec §5.2, §5.3).
namespace cause {
inline constexpr u32 kInterrupt = 1u << 31;
inline constexpr unsigned kSubcodeLo = 8;  // [15:8]: page-fault subcode
}  // namespace cause

// TVEC fields (spec §5.4).
namespace tvec {
inline constexpr u32 kVectored = 1u << 0;  // mode bit: 0 = direct, 1 = vectored
inline constexpr u32 kBaseMask = 0xFFFF'FFFC;
}  // namespace tvec

// PTBR fields (spec §7, §8.1).
namespace ptbr {
inline constexpr u32 kEnable = 1u << 0;  // EN: paging on
inline constexpr unsigned kAsidLo = 4;   // [11:4]
inline constexpr unsigned kPpnLo = 12;   // [31:12]
}  // namespace ptbr

inline constexpr u32 kCpuIdValue = 0x0001'0000;  // ISA 1.0 (§7)

}  // namespace asteria::isa
