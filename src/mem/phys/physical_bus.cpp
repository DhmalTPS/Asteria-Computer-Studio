#include "mem/phys/physical_bus.hpp"

#include <algorithm>

#include "common/assert.hpp"
#include "common/bits.hpp"

namespace asteria::mem::phys {

using memory_map::kMmioBase;
using memory_map::kMmioSize;
using memory_map::kRamBase;
using memory_map::kRomBase;
using memory_map::kRomSize;
using sim::Access;

namespace {

enum class Region : u8 { Rom, Ram, Mmio, Unpopulated };

// A range is "in" a region only if it lies entirely inside it.
bool within(u32 paddr, u64 size, u32 base, u64 region_size) {
    const u64 start = paddr;
    return start >= base && start + size <= u64{base} + region_size;
}

void check_size_and_alignment(u32 paddr, unsigned size) {
    ASTERIA_ASSERT(size == 1 || size == 2 || size == 4, "access size must be 1, 2 or 4");
    ASTERIA_ASSERT(bits::is_aligned(paddr, size), "misaligned access reached the bus");
}

}  // namespace

Result<PhysicalBus> PhysicalBus::create(const BusConfig& config) {
    if (config.ram_size == 0 || config.ram_size % memory_map::kPageSize != 0) {
        return fail("RAM size must be a non-zero multiple of {} bytes (got {})",
                    memory_map::kPageSize, config.ram_size);
    }
    if (config.ram_size > memory_map::kMaxRamSize) {
        return fail("RAM size must be at most {} bytes (got {})", memory_map::kMaxRamSize,
                    config.ram_size);
    }
    return PhysicalBus{config.ram_size};
}

PhysicalBus::PhysicalBus(u32 ram_size) : rom_(kRomSize, u8{0}), ram_(ram_size, u8{0}) {}

std::span<u8> PhysicalBus::backing(u32 paddr, u64 size) {
    if (within(paddr, size, kRomBase, rom_.size())) {
        return std::span<u8>{rom_}.subspan(paddr - kRomBase, static_cast<std::size_t>(size));
    }
    if (within(paddr, size, kRamBase, ram_.size())) {
        return std::span<u8>{ram_}.subspan(paddr - kRamBase, static_cast<std::size_t>(size));
    }
    return {};
}

std::span<const u8> PhysicalBus::backing(u32 paddr, u64 size) const {
    return const_cast<PhysicalBus*>(this)->backing(paddr, size);
}

bool PhysicalBus::accepts(u32 paddr, unsigned size, Access access) const {
    check_size_and_alignment(paddr, size);

    // Aligned accesses of at most 4 bytes never straddle a region: every region starts and
    // ends on a page boundary.
    Region region = Region::Unpopulated;
    if (within(paddr, size, kRomBase, rom_.size())) region = Region::Rom;
    else if (within(paddr, size, kRamBase, ram_.size())) region = Region::Ram;
    else if (within(paddr, size, kMmioBase, kMmioSize)) region = Region::Mmio;

    switch (region) {
        case Region::Rom:
            return access == Access::Fetch || access == Access::Load || access == Access::WalkRead;
        case Region::Ram:
            return true;
        case Region::Mmio:
            // Word-sized loads and stores to a device page. Devices arrive in M8, so none
            // exist yet and every MMIO access is a bus error.
            return false;
        case Region::Unpopulated:
            return false;
    }
    return false;
}

std::optional<u32> PhysicalBus::read(u32 paddr, unsigned size, Access access) {
    if (!accepts(paddr, size, access)) {
        return std::nullopt;
    }
    const std::span<const u8> bytes = backing(paddr, size);
    ASTERIA_ASSERT(bytes.size() == size, "accepted access has no backing store");
    u32 value = 0;
    for (unsigned i = 0; i < size; ++i) {
        value |= u32{bytes[i]} << (8 * i);  // little-endian (spec §1)
    }
    return value;
}

bool PhysicalBus::write(u32 paddr, unsigned size, u32 value, Access access) {
    if (!accepts(paddr, size, access)) {
        return false;
    }
    const std::span<u8> bytes = backing(paddr, size);
    ASTERIA_ASSERT(bytes.size() == size, "accepted access has no backing store");
    for (unsigned i = 0; i < size; ++i) {
        bytes[i] = static_cast<u8>(value >> (8 * i));
    }
    return true;
}

bool PhysicalBus::debug_read(u32 paddr, std::span<u8> out) const {
    const std::span<const u8> bytes = backing(paddr, out.size());
    if (bytes.size() != out.size()) {
        return false;
    }
    std::ranges::copy(bytes, out.begin());
    return true;
}

bool PhysicalBus::debug_write(u32 paddr, std::span<const u8> in) {
    const std::span<u8> bytes = backing(paddr, in.size());
    if (bytes.size() != in.size()) {
        return false;
    }
    std::ranges::copy(in, bytes.begin());
    return true;
}

}  // namespace asteria::mem::phys
