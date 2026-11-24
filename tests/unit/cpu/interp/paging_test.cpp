#include <catch2/catch_test_macros.hpp>

#include "cpu/interp/page_walk.hpp"
#include "test_machine.hpp"

using namespace asteria;
using namespace asteria::test;
using cpu::interp::StepKind;
using cpu::interp::walk;
using sim::Access;
namespace pte = asteria::cpu::interp::pte;
using C = isa::Csr;

namespace {

// Layout used by every test (physical addresses):
//   0x8001_0000  root table      0x8001_1000  level-0 table
//   0x8002_0000  a 4 KiB data page
constexpr u32 kRoot = kRam + 0x1'0000;
constexpr u32 kLeafTable = kRam + 0x1'1000;
constexpr u32 kDataPage = kRam + 0x2'0000;

constexpr u32 entry(u32 paddr, u32 flags) { return (paddr & 0xFFFF'F000u) | flags; }
constexpr u32 slot(u32 table, u32 index) { return table + index * 4; }

// VA 0x8000_0000 -> identity 4 MiB superpage for the kernel (VPN1 = 0x200).
// VA 0x0040_0000 -> kDataPage, through the level-0 table (VPN1 = 1, VPN0 = 0).
void map_standard(TestMachine& m, u32 data_flags) {
    m.poke(slot(kRoot, 0x200), entry(kRam, pte::kV | pte::kR | pte::kW | pte::kX | pte::kG));
    m.poke(slot(kRoot, 1), entry(kLeafTable, pte::kV));
    m.poke(slot(kLeafTable, 0), entry(kDataPage, data_flags));
}

constexpr u32 kPtbr = kRoot | isa::ptbr::kEnable;
constexpr u32 kUserVa = 0x0040'0000;

}  // namespace

TEST_CASE("page walk: 4 KiB pages and 4 MiB superpages translate (spec section 8.4)") {
    TestMachine m;
    map_standard(m, pte::kV | pte::kR);

    const auto small = walk(m.bus, kPtbr, kUserVa + 0x123, Access::Load, false);
    REQUIRE_FALSE(small.trap);
    CHECK(small.paddr == kDataPage + 0x123);

    const auto super = walk(m.bus, kPtbr, kRam + 0x3F'FFFC, Access::Fetch, false);
    REQUIRE_FALSE(super.trap);
    CHECK(super.paddr == kRam + 0x3F'FFFC);  // VPN0 and offset pass through
}

TEST_CASE("page walk: reports the A/D update instead of writing it") {
    TestMachine m;
    map_standard(m, pte::kV | pte::kR | pte::kW);
    const u32 before = m.peek(slot(kLeafTable, 0));

    const auto load = walk(m.bus, kPtbr, kUserVa, Access::Load, false);
    CHECK(load.pte_address == slot(kLeafTable, 0));
    CHECK(load.updated_pte == (before | pte::kA));

    const auto store = walk(m.bus, kPtbr, kUserVa, Access::Store, false);
    CHECK(store.updated_pte == (before | pte::kA | pte::kD));
    CHECK(m.peek(slot(kLeafTable, 0)) == before);  // nothing written

    m.poke(slot(kLeafTable, 0), before | pte::kA | pte::kD);
    CHECK_FALSE(walk(m.bus, kPtbr, kUserVa, Access::Store, false).pte_address.has_value());
}

TEST_CASE("page walk: faults carry the right code, subcode and address (spec section 5.2)") {
    TestMachine m;
    map_standard(m, pte::kV | pte::kR);
    const auto cause_of = [&](u32 va, Access a, bool user) {
        const auto w = walk(m.bus, kPtbr, va, a, user);
        REQUIRE(w.trap.has_value());
        CHECK(w.trap->badvaddr == va);
        return w.trap->cause;
    };

    SECTION("not present at level 1 (subcode 0)") {
        CHECK(cause_of(0x0080'0010, Access::Load, false) == 9u);
    }
    SECTION("not present at level 0") {
        CHECK(cause_of(kUserVa + 0x1000, Access::Fetch, false) == 1u);
    }
    SECTION("a pointer at level 0 is not present") {
        m.poke(slot(kLeafTable, 2), entry(kDataPage, pte::kV));
        CHECK(cause_of(kUserVa + 0x2000, Access::Load, false) == 9u);
    }
    SECTION("W without R is reserved (subcode 1)") {
        m.poke(slot(kLeafTable, 0), entry(kDataPage, pte::kV | pte::kW));
        CHECK(cause_of(kUserVa, Access::Load, false) == 0x109u);
    }
    SECTION("a misaligned superpage (subcode 2)") {
        m.poke(slot(kRoot, 3), entry(kRam + 0x1000, pte::kV | pte::kR));
        CHECK(cause_of(0x00C0'0000, Access::Load, false) == 0x209u);
    }
    SECTION("permissions: store to a read-only page, fetch from a non-X page") {
        CHECK(cause_of(kUserVa, Access::Store, false) == 0x10Au);
        CHECK(cause_of(kUserVa, Access::Fetch, false) == 0x101u);
    }
    SECTION("SWAP needs both R and W") {
        m.poke(slot(kLeafTable, 0), entry(kDataPage, pte::kV | pte::kR | pte::kW));
        CHECK_FALSE(walk(m.bus, kPtbr, kUserVa, Access::Swap, false).trap);
        m.poke(slot(kLeafTable, 0), entry(kDataPage, pte::kV | pte::kR));
        CHECK(cause_of(kUserVa, Access::Swap, false) == 0x10Au);
    }
    SECTION("User mode needs U; Kernel mode does not") {
        CHECK(cause_of(kUserVa, Access::Load, true) == 0x109u);
        m.poke(slot(kLeafTable, 0), entry(kDataPage, pte::kV | pte::kR | pte::kU));
        CHECK_FALSE(walk(m.bus, kPtbr, kUserVa, Access::Load, true).trap);
        CHECK_FALSE(walk(m.bus, kPtbr, kUserVa, Access::Load, false).trap);
    }
    SECTION("a page table outside ROM/RAM is a bus error of the access's type") {
        const auto w = walk(m.bus, 0xF000'0000 | isa::ptbr::kEnable, kUserVa, Access::Store, false);
        REQUIRE(w.trap);
        CHECK(w.trap->cause == 12u);
    }
}

TEST_CASE("interpreter with paging: loads and stores set A and D in the leaf PTE") {
    TestMachine m;
    map_standard(m, pte::kV | pte::kR | pte::kW);
    m.poke(kDataPage + 8, 0x1234'5678);
    m.load(kRam, {
        I(Op::Lui, 1, 0, 0, 0x8001),
        Csr(Op::Csrrw, 0, C::Ptbr, 1),   // PTBR = root (EN = 0 yet)
        Csr(Op::Csrrsi, 0, C::Ptbr, 1),  // EN = 1: the next fetch is translated
        I(Op::Lui, 2, 0, 0, 0x0040),     // R2 = user VA
        I(Op::Lw, 3, 2, 0, 8),
    });
    for (int i = 0; i < 5; ++i) REQUIRE(m.cpu.step().kind == StepKind::Retired);
    CHECK(m.reg(3) == 0x1234'5678u);
    CHECK((m.peek(slot(kLeafTable, 0)) & (pte::kA | pte::kD)) == pte::kA);
    CHECK((m.peek(slot(kRoot, 0x200)) & pte::kA) != 0);  // the kernel superpage was fetched

    m.load(kRam + 20, {I(Op::Sw, 0, 2, 3, 0)});
    REQUIRE(m.cpu.step().kind == StepKind::Retired);
    CHECK((m.peek(slot(kLeafTable, 0)) & pte::kD) != 0);
    CHECK(m.peek(kDataPage) == 0x1234'5678u);
}

TEST_CASE("interpreter with paging: a faulting access changes nothing at all") {
    TestMachine m;
    map_standard(m, pte::kV | pte::kR);  // read-only data page
    m.set_csr(C::Tvec, kRam + 0x400);
    m.set_csr(C::Ptbr, kPtbr);
    m.load(kRam, {I(Op::Lui, 2, 0, 0, 0x0040), I(Op::Addi, 3, 0, 0, 7), I(Op::Sw, 0, 2, 3, 0)});
    const u32 pte_before = m.peek(slot(kLeafTable, 0));

    for (int i = 0; i < 2; ++i) REQUIRE(m.cpu.step().kind == StepKind::Retired);
    const auto r = m.cpu.step();
    CHECK(r.kind == StepKind::Trapped);
    CHECK(m.csr(C::Cause) == 0x10Au);
    CHECK(m.csr(C::BadVaddr) == kUserVa);
    CHECK(m.peek(kDataPage) == 0u);
    CHECK(m.peek(slot(kLeafTable, 0)) == pte_before);  // no A bit for a failed access
}

TEST_CASE("interpreter with paging: the A bit is not set when the access itself bus-errors") {
    TestMachine m;
    map_standard(m, pte::kV | pte::kR);
    m.poke(slot(kLeafTable, 1), entry(0xF000'0000, pte::kV | pte::kR));  // maps MMIO
    m.set_csr(C::Tvec, kRam + 0x400);
    m.set_csr(C::Ptbr, kPtbr);
    m.load(kRam, {I(Op::Lui, 2, 0, 0, 0x0040), I(Op::Lw, 3, 2, 0, 0x1000)});

    (void)m.cpu.step();
    CHECK(m.cpu.step().kind == StepKind::Trapped);
    CHECK(m.csr(C::Cause) == 11u);
    CHECK(m.csr(C::BadVaddr) == kUserVa + 0x1000);
    CHECK((m.peek(slot(kLeafTable, 1)) & pte::kA) == 0u);
}

TEST_CASE("interpreter with paging: a PTE that needs A/D but sits in ROM is a bus error") {
    TestMachine m;
    // Root table in ROM at 0x1000 (written by the host): one superpage for the kernel with
    // A already set, and a second mapping of the same RAM at VA 0x0040_0000 with A clear.
    // (Same RAM, so the data access itself would succeed: only the write-back fails.)
    m.poke(0x1000 + 0x200 * 4,
           entry(kRam, pte::kV | pte::kR | pte::kW | pte::kX | pte::kA | pte::kD));
    m.poke(0x1000 + 1 * 4, entry(kRam, pte::kV | pte::kR));
    m.set_csr(C::Tvec, kRam + 0x400);
    m.set_csr(C::Ptbr, 0x1000 | isa::ptbr::kEnable);
    m.load(kRam, {I(Op::Lui, 2, 0, 0, 0x0040), I(Op::Lw, 3, 2, 0, 0)});

    CHECK(m.cpu.step().kind == StepKind::Retired);  // fetches need no write-back
    CHECK(m.cpu.step().kind == StepKind::Trapped);
    CHECK(m.csr(C::Cause) == 11u);
    CHECK(m.reg(3) == 0u);
}
