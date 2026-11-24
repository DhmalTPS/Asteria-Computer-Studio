#pragma once
// Machine: the one place that wires concrete parts into a computer (OVERVIEW.md section 2).
//
// M1's machine is the smallest useful one: a physical bus (ROM + RAM) and the reference
// interpreter, in bare-metal mode (docs/platform/MEMORY-MAP.md section 3): paging off, Kernel
// mode, the program loaded by the host, PC = entry and sp = top of RAM.
//
// run() steps until something ends the program, and says why:
//   Halted          HALT executed; the exit code is a0
//   StepLimit       the step budget ran out (an infinite loop, or just a long program)
//   StuckTrap       the same trap (cause, EPC, BADVADDR) was taken twice in a row: the
//                   handler itself is faulting, so nothing can ever change
//   WaitingForever  WFI with no interrupt source that could wake it (no devices before M8)

#include <functional>
#include <memory>
#include <optional>

#include "common/result.hpp"
#include "common/types.hpp"
#include "cpu/core/trap.hpp"
#include "cpu/interp/interpreter.hpp"
#include "mem/phys/physical_bus.hpp"
#include "toolchain/axf/axf.hpp"

namespace asteria::machine {

struct MachineConfig {
    u32 ram_size = memory_map::kDefaultRamSize;
};

enum class StopReason : u8 { Halted, StepLimit, StuckTrap, WaitingForever };

struct RunReport {
    StopReason reason = StopReason::Halted;
    u64 steps = 0;     // in this run() call
    u64 traps = 0;     // traps taken in this run() call
    u32 exit_code = 0; // a0 at HALT
    std::optional<cpu::core::Trap> first_trap;  // with the PC where each happened
    u32 first_trap_pc = 0;
    std::optional<cpu::core::Trap> last_trap;
    u32 last_trap_pc = 0;
};

using StepObserver = std::function<void(const cpu::interp::StepResult&)>;

class Machine {
public:
    // Fails on an invalid configuration. The machine is not movable (the CPU holds a
    // reference to the bus), hence the unique_ptr.
    [[nodiscard]] static Result<std::unique_ptr<Machine>> create(const MachineConfig& config);

    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    // Resets the CPU and loads the program: every segment into RAM (the part past its data
    // zero-filled), PC = entry, sp = top of RAM. Fails if a segment does not fit in RAM.
    [[nodiscard]] Result<void> load(const toolchain::axf::Executable& exe);

    RunReport run(u64 max_steps, const StepObserver& observer = {});

    [[nodiscard]] cpu::interp::Interpreter& cpu() noexcept { return cpu_; }
    [[nodiscard]] mem::phys::PhysicalBus& bus() noexcept { return bus_; }

private:
    explicit Machine(mem::phys::PhysicalBus bus) : bus_(std::move(bus)), cpu_(bus_) {}

    mem::phys::PhysicalBus bus_;
    cpu::interp::Interpreter cpu_;  // declared after bus_: it is built with a reference to it
};

}  // namespace asteria::machine
