#pragma once
// Exception codes and page-fault subcodes (spec §5.2), and the interrupt lines (§5.3).
//
// The numeric order matters: when one instruction could raise several exceptions, the
// LOWEST code wins. The order also follows the life of an instruction (fetch, decode,
// privilege, address check, translation, bus), so an implementation that checks in that
// order gets the priority right without any special code.

#include <string_view>

#include "common/types.hpp"

namespace asteria::isa {

enum class Exception : u8 {
    InstrMisaligned = 0,  // JALR to an address that isn't a multiple of 4
    InstrPageFault = 1,
    InstrBusError = 2,
    IllegalInstruction = 3,
    PrivilegeViolation = 4,
    Breakpoint = 5,       // EBREAK
    EnvironmentCall = 6,  // ECALL
    LoadMisaligned = 7,
    StoreMisaligned = 8,
    LoadPageFault = 9,
    StorePageFault = 10,
    LoadBusError = 11,
    StoreBusError = 12,
};

inline constexpr unsigned kExceptionCount = 13;

// CAUSE[15:8] for page faults (spec §5.2).
enum class PageFaultReason : u8 {
    NotPresent = 0,           // V = 0 at some level
    PermissionDenied = 1,     // R/W/X/U check failed, or the reserved W=1, R=0 combination
    MisalignedSuperpage = 2,  // a level-1 leaf whose PPN[9:0] is not zero
};

// Exceptions that report the faulting address in BADVADDR (spec §5.2 table).
[[nodiscard]] constexpr bool sets_badvaddr(Exception e) noexcept {
    switch (e) {
        case Exception::IllegalInstruction:
        case Exception::PrivilegeViolation:
        case Exception::Breakpoint:
        case Exception::EnvironmentCall: return false;
        default: return true;
    }
}

[[nodiscard]] constexpr bool is_page_fault(Exception e) noexcept {
    return e == Exception::InstrPageFault || e == Exception::LoadPageFault ||
           e == Exception::StorePageFault;
}

[[nodiscard]] constexpr std::string_view name(Exception e) noexcept {
    switch (e) {
        case Exception::InstrMisaligned: return "InstrMisaligned";
        case Exception::InstrPageFault: return "InstrPageFault";
        case Exception::InstrBusError: return "InstrBusError";
        case Exception::IllegalInstruction: return "IllegalInstruction";
        case Exception::PrivilegeViolation: return "PrivilegeViolation";
        case Exception::Breakpoint: return "Breakpoint";
        case Exception::EnvironmentCall: return "EnvironmentCall";
        case Exception::LoadMisaligned: return "LoadMisaligned";
        case Exception::StoreMisaligned: return "StoreMisaligned";
        case Exception::LoadPageFault: return "LoadPageFault";
        case Exception::StorePageFault: return "StorePageFault";
        case Exception::LoadBusError: return "LoadBusError";
        case Exception::StoreBusError: return "StoreBusError";
    }
    return "?";
}

// Interrupt lines (spec §5.3). IRQ i is bit i of IPEND and IMASK; the lowest pending,
// enabled line wins.
inline constexpr unsigned kIrqCount = 8;

}  // namespace asteria::isa
