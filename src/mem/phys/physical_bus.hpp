#pragma once
// PhysicalBus: Boot ROM + RAM + the MMIO window, routed by physical address
// (docs/platform/MEMORY-MAP.md, D12). The untimed implementation of sim::FunctionalMemory.
//
// Region rules, in one place (MEMORY-MAP section 1):
//
//   Region        Fetch  Load  Store  Swap  WalkRead  WalkWrite
//   ROM            yes   yes    -      -      yes        -
//   RAM            yes   yes   yes    yes     yes       yes
//   MMIO            -    dev   dev     -       -         -       dev = word-sized, and only
//   unpopulated     -     -     -      -       -         -             where a device exists
//
// No devices exist until M8, so in M1 every MMIO access fails with a bus error.
// Both ROM and RAM start zeroed (D14: determinism).

#include <optional>
#include <span>
#include <vector>

#include "common/memory_map.hpp"
#include "common/result.hpp"
#include "common/types.hpp"
#include "sim/functional_memory.hpp"

namespace asteria::mem::phys {

struct BusConfig {
    u32 ram_size = memory_map::kDefaultRamSize;  // whole pages, 1 page .. 1 GiB
};

class PhysicalBus final : public sim::FunctionalMemory {
public:
    // Fails if the configuration is invalid (bad input, so a Result, not an assert).
    [[nodiscard]] static Result<PhysicalBus> create(const BusConfig& config);

    [[nodiscard]] u32 ram_size() const noexcept { return static_cast<u32>(ram_.size()); }

    [[nodiscard]] bool accepts(u32 paddr, unsigned size, sim::Access access) const override;
    [[nodiscard]] std::optional<u32> read(u32 paddr, unsigned size, sim::Access access) override;
    [[nodiscard]] bool write(u32 paddr, unsigned size, u32 value, sim::Access access) override;
    [[nodiscard]] bool debug_read(u32 paddr, std::span<u8> out) const override;
    [[nodiscard]] bool debug_write(u32 paddr, std::span<const u8> in) override;

private:
    explicit PhysicalBus(u32 ram_size);

    // The backing bytes for [paddr, paddr + size) if the whole range lies in ROM or RAM,
    // otherwise an empty span. Used by both the CPU path and the debug path.
    [[nodiscard]] std::span<u8> backing(u32 paddr, u64 size);
    [[nodiscard]] std::span<const u8> backing(u32 paddr, u64 size) const;

    std::vector<u8> rom_;
    std::vector<u8> ram_;
};

}  // namespace asteria::mem::phys
