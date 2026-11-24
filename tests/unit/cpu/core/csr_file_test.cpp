#include <catch2/catch_test_macros.hpp>

#include "cpu/core/csr_file.hpp"
#include "isa/csrs.hpp"

using namespace asteria;
using namespace asteria::cpu::core;
using isa::Csr;
using isa::Exception;

namespace {
constexpr u16 n(Csr c) { return static_cast<u16>(c); }
}  // namespace

TEST_CASE("CsrFile resets to the values in spec section 7") {
    CsrFile csr;
    csr.write(n(Csr::Status), 0xF);
    csr.write(n(Csr::Scratch), 123);
    csr.count_cycle();
    csr.reset();
    for (const isa::CsrInfo& info : isa::kCsrs) {
        INFO(info.name);
        CHECK(csr.read(n(info.csr)) == info.reset_value);
    }
    CHECK(csr.kernel_mode());
    CHECK_FALSE(csr.interrupts_enabled());
}

TEST_CASE("CsrFile writes keep only the defined bits") {
    CsrFile csr;
    for (const isa::CsrInfo& info : isa::kCsrs) {
        if (info.read_only) continue;
        INFO(info.name);
        csr.write(n(info.csr), 0xFFFF'FFFF);
        CHECK(csr.read(n(info.csr)) == info.writable_mask);
        csr.write(n(info.csr), 0);
        CHECK(csr.read(n(info.csr)) == 0u);
    }
}

TEST_CASE("CsrFile access checks: unknown or read-only before privilege") {
    CsrFile csr;
    // Kernel mode.
    CHECK(csr.check_access(0x123, false) == Exception::IllegalInstruction);
    CHECK(csr.check_access(n(Csr::Cycle), true) == Exception::IllegalInstruction);
    CHECK_FALSE(csr.check_access(n(Csr::Cycle), false).has_value());
    CHECK_FALSE(csr.check_access(n(Csr::Epc), true).has_value());

    csr.write(n(Csr::Status), 0);  // User mode
    CHECK(csr.check_access(n(Csr::Epc), false) == Exception::PrivilegeViolation);
    CHECK(csr.check_access(n(Csr::Ipend), false) == Exception::PrivilegeViolation);
    CHECK(csr.check_access(n(Csr::Ipend), true) == Exception::IllegalInstruction);  // 3 beats 4
    CHECK_FALSE(csr.check_access(n(Csr::Instret), false).has_value());
    CHECK_FALSE(csr.check_access(n(Csr::CpuId), false).has_value());
}

TEST_CASE("CsrFile counters read as two 32-bit halves") {
    CsrFile csr;
    for (int i = 0; i < 3; ++i) csr.count_cycle();
    csr.count_retired();
    CHECK(csr.read(n(Csr::Cycle)) == 3u);
    CHECK(csr.read(n(Csr::CycleH)) == 0u);
    CHECK(csr.read(n(Csr::Instret)) == 1u);
    CHECK(csr.read(n(Csr::CpuId)) == 0x0001'0000u);
}

TEST_CASE("Trap entry saves IE and KM, enters Kernel mode and records the trap (spec section 5.1)") {
    CsrFile csr;
    csr.write(n(Csr::Tvec), 0x8000'1000);
    csr.write(n(Csr::BadVaddr), 0x5555);
    csr.write(n(Csr::Status), isa::status::kIe);  // User mode, interrupts on

    const u32 target = csr.enter_trap(Trap::exception(Exception::EnvironmentCall), 0x8000'0040);
    CHECK(target == 0x8000'1000u);
    CHECK(csr.read(n(Csr::Status)) == (isa::status::kKm | isa::status::kPie));
    CHECK(csr.read(n(Csr::Epc)) == 0x8000'0040u);
    CHECK(csr.read(n(Csr::Cause)) == 6u);
    CHECK(csr.read(n(Csr::BadVaddr)) == 0x5555u);  // unchanged: not an address exception

    // ERET restores IE and KM; PIE and PKM keep their values.
    CHECK(csr.eret() == 0x8000'0040u);
    CHECK(csr.read(n(Csr::Status)) == (isa::status::kIe | isa::status::kPie));
}

TEST_CASE("Trap entry records page-fault subcodes and the faulting address") {
    CsrFile csr;
    const Trap t = Trap::page_fault(Exception::StorePageFault,
                                    isa::PageFaultReason::MisalignedSuperpage, 0x1234'5678);
    (void)csr.enter_trap(t, 0x8000'0000);
    CHECK(csr.read(n(Csr::Cause)) == 0x0000'020Au);
    CHECK(csr.read(n(Csr::BadVaddr)) == 0x1234'5678u);
    CHECK(csr.read(n(Csr::Status)) == (isa::status::kKm | isa::status::kPkm));
}

TEST_CASE("Vectored mode sends only interrupts to base + 4 x (1 + irq) (spec section 5.4)") {
    CsrFile csr;
    csr.write(n(Csr::Tvec), 0x8000'2000 | isa::tvec::kVectored);
    CHECK(csr.enter_trap(Trap::interrupt(0), 0) == 0x8000'2004u);
    CHECK(csr.enter_trap(Trap::interrupt(7), 0) == 0x8000'2020u);
    CHECK(csr.enter_trap(Trap::exception(Exception::Breakpoint), 0) == 0x8000'2000u);
    CHECK(csr.read(n(Csr::Cause)) == 5u);

    csr.write(n(Csr::Tvec), 0x8000'2000);  // direct
    CHECK(csr.enter_trap(Trap::interrupt(3), 0) == 0x8000'2000u);
    CHECK(csr.read(n(Csr::Cause)) == 0x8000'0003u);
}
