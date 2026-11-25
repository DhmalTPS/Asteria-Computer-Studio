#include "machine/machine.hpp"

#include <algorithm>
#include <vector>

#include "common/assert.hpp"
#include "common/memory_map.hpp"
#include "isa/registers.hpp"

namespace asteria::machine {

using cpu::interp::StepKind;

Result<std::unique_ptr<Machine>> Machine::create(const MachineConfig& config) {
    Result<mem::phys::PhysicalBus> bus = mem::phys::PhysicalBus::create({config.ram_size});
    if (!bus) {
        return std::unexpected(bus.error());
    }
    return std::unique_ptr<Machine>(new Machine(std::move(*bus)));
}

Result<void> Machine::load(const toolchain::axf::Executable& exe) {
    if (Result<void> ok = toolchain::axf::validate(exe); !ok) {
        return fail("invalid executable: {}", ok.error().message);
    }
    const u64 ram_end = u64{memory_map::kRamBase} + bus_.ram_size();
    for (const toolchain::axf::Segment& s : exe.segments) {
        if (s.vaddr < memory_map::kRamBase || u64{s.vaddr} + s.mem_size > ram_end) {
            return fail("segment at 0x{:08X} ({} bytes) does not fit in RAM (0x{:08X}-0x{:08X})",
                        s.vaddr, s.mem_size, memory_map::kRamBase, ram_end - 1);
        }
    }

    cpu_.reset();
    for (const toolchain::axf::Segment& s : exe.segments) {
        // Write the whole segment, zeros included, so a second load can't leave old bytes.
        std::vector<u8> image(s.mem_size, 0);
        std::ranges::copy(s.data, image.begin());
        const bool written = bus_.debug_write(s.vaddr, image);
        ASTERIA_ASSERT(written, "segment was checked to fit in RAM");
    }
    // The interpreter has no instruction cache, so nothing needs FENCE.I here. The timed
    // machines (M3+) will flush their caches at this point (spec §9).
    cpu_.set_pc(exe.entry);
    cpu_.registers().write(isa::reg::kSp, static_cast<u32>(ram_end));  // stack grows down
    return {};
}

RunReport Machine::run(u64 max_steps, const StepObserver& observer) {
    RunReport report;
    bool previous_was_trap = false;
    while (true) {
        if (report.steps >= max_steps) {
            report.reason = StopReason::StepLimit;
            return report;
        }
        const cpu::interp::StepResult r = cpu_.step();
        ++report.steps;
        if (observer) observer(r);

        switch (r.kind) {
            case StepKind::Retired:
                previous_was_trap = false;
                break;
            case StepKind::Halted:
                report.reason = StopReason::Halted;
                report.exit_code = cpu_.registers().read(isa::reg::kA0);
                return report;
            case StepKind::Waiting:
                // No device can raise a line before M8, so nothing will ever wake it.
                report.reason = StopReason::WaitingForever;
                return report;
            case StepKind::Trapped: {
                ++report.traps;
                const bool same = previous_was_trap && report.last_trap == r.trap &&
                                  report.last_trap_pc == r.pc;
                if (!report.first_trap) {
                    report.first_trap = r.trap;
                    report.first_trap_pc = r.pc;
                }
                report.last_trap = r.trap;
                report.last_trap_pc = r.pc;
                previous_was_trap = true;
                if (same) {
                    report.reason = StopReason::StuckTrap;
                    return report;
                }
                break;
            }
        }
    }
}

}  // namespace asteria::machine
