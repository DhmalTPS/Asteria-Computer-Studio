#include <catch2/catch_test_macros.hpp>

#include <array>

#include "common/memory_map.hpp"
#include "mem/phys/physical_bus.hpp"

using namespace asteria;
using namespace asteria::mem::phys;
using sim::Access;
namespace mm = asteria::memory_map;

namespace {

PhysicalBus make_bus(u32 ram_size = 64 * 1024) {
    auto bus = PhysicalBus::create({ram_size});
    REQUIRE(bus.has_value());
    return std::move(*bus);
}

constexpr std::array kAllAccesses = {Access::Fetch, Access::Load, Access::Store,
                                     Access::Swap, Access::WalkRead, Access::WalkWrite};

}  // namespace

TEST_CASE("PhysicalBus validates its RAM size") {
    CHECK(PhysicalBus::create({4096}).has_value());
    CHECK(PhysicalBus::create({mm::kDefaultRamSize}).has_value());
    CHECK_FALSE(PhysicalBus::create({0}).has_value());
    CHECK_FALSE(PhysicalBus::create({4096 + 4}).has_value());
    CHECK_FALSE(PhysicalBus::create({mm::kMaxRamSize + 4096}).has_value());
    CHECK(PhysicalBus::create({100}).error().message ==
          "RAM size must be a non-zero multiple of 4096 bytes (got 100)");
}

TEST_CASE("PhysicalBus applies the memory map's access rules per region") {
    PhysicalBus bus = make_bus();
    const u32 ram_end = mm::kRamBase + bus.ram_size();

    for (const Access a : kAllAccesses) {
        // ROM: read, execute, page-table reads. Never written by the machine.
        const bool rom_ok = a == Access::Fetch || a == Access::Load || a == Access::WalkRead;
        CHECK(bus.accepts(0x0000'0000, 4, a) == rom_ok);
        CHECK(bus.accepts(mm::kRomSize - 4, 4, a) == rom_ok);
        // RAM: everything, up to the last byte of the configured size.
        CHECK(bus.accepts(mm::kRamBase, 4, a));
        CHECK(bus.accepts(ram_end - 4, 4, a));
        // Just past ROM and RAM, MMIO (no devices before M8) and the top: bus errors.
        CHECK_FALSE(bus.accepts(mm::kRomSize, 4, a));
        CHECK_FALSE(bus.accepts(ram_end, 4, a));
        CHECK_FALSE(bus.accepts(mm::kMmioBase, 4, a));
        CHECK_FALSE(bus.accepts(0xFFFF'FFFC, 4, a));
    }
}

TEST_CASE("PhysicalBus reads and writes little-endian values of every size") {
    PhysicalBus bus = make_bus();
    REQUIRE(bus.write(mm::kRamBase, 4, 0x1122'3344, Access::Store));
    CHECK(bus.read(mm::kRamBase, 1, Access::Load) == 0x44u);
    CHECK(bus.read(mm::kRamBase + 1, 1, Access::Load) == 0x33u);
    CHECK(bus.read(mm::kRamBase, 2, Access::Load) == 0x3344u);
    CHECK(bus.read(mm::kRamBase + 2, 2, Access::Load) == 0x1122u);

    REQUIRE(bus.write(mm::kRamBase + 1, 1, 0xFFFF'FFAA, Access::Store));  // only the low byte
    CHECK(bus.read(mm::kRamBase, 4, Access::Load) == 0x1122'AA44u);

    std::array<u8, 4> bytes{};
    REQUIRE(bus.debug_read(mm::kRamBase, bytes));
    CHECK(bytes == std::array<u8, 4>{0x44, 0xAA, 0x22, 0x11});
}

TEST_CASE("PhysicalBus starts zeroed and refuses writes to ROM without changing it") {
    PhysicalBus bus = make_bus();
    CHECK(bus.read(0, 4, Access::Fetch) == 0u);
    CHECK(bus.read(mm::kRamBase + 0x100, 4, Access::Load) == 0u);

    CHECK_FALSE(bus.write(0, 4, 0xDEAD'BEEF, Access::Store));
    CHECK(bus.read(0, 4, Access::Load) == 0u);
    CHECK_FALSE(bus.read(mm::kMmioBase, 4, Access::Load).has_value());
}

TEST_CASE("PhysicalBus debug access ignores permissions but not unpopulated addresses") {
    PhysicalBus bus = make_bus(4096);
    const std::array<u8, 4> word{0x04, 0x00, 0x00, 0x00};

    CHECK(bus.debug_write(0x0000'0010, word));  // the loader may write ROM
    CHECK(bus.read(0x10, 4, Access::Fetch) == 0x0000'0004u);

    // The whole range must exist: one byte past the end of RAM fails, and nothing is written.
    const std::array<u8, 8> eight{1, 2, 3, 4, 5, 6, 7, 8};
    CHECK_FALSE(bus.debug_write(mm::kRamBase + 4096 - 4, eight));
    CHECK(bus.read(mm::kRamBase + 4096 - 4, 4, Access::Load) == 0u);
    CHECK_FALSE(bus.debug_write(mm::kMmioBase, word));
    CHECK_FALSE(bus.debug_write(0xFFFF'FFFE, word));  // would wrap past 2^32

    std::array<u8, 4> out{};
    CHECK_FALSE(bus.debug_read(0x0001'0000, out));
}
