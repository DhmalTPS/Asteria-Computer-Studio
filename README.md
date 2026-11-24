# Asteria Computer Studio

[![CI](https://github.com/DhmalTPS/Asteria-Computer-Studio/actions/workflows/ci.yml/badge.svg)](https://github.com/DhmalTPS/Asteria-Computer-Studio/actions/workflows/ci.yml)

A complete computer, designed and built in software: an original 32-bit instruction set (**ASTERIA-32**),
three processor microarchitectures that run it (single-cycle, multi-cycle, 5-stage pipeline), a
cycle-aware memory system (caches, MMU/TLB, DRAM controller with real timing), interrupt-driven devices
with DMA, a self-written toolchain (assembler, linker, C compiler, debugger), and **AsteriaOS**, which runs
on the virtual machine. All of it lives in one Windows desktop application where you can see every cycle.

> Status: **M1 in progress.** The instruction set exists as code (instruction table, encoder, decoder,
> disassembler), the reference interpreter executes it (every instruction, CSRs, traps, interrupts,
> privilege and paging), and the assembler turns `.asm` source into `.ast` executables that
> `asteria run` executes. Next: the ISA conformance suite. See [docs/ROADMAP.md](docs/ROADMAP.md).

No existing emulator, assembler, cache or memory simulator is used. Third-party code is limited to
the GUI (Dear ImGui, ImPlot) and the test framework (Catch2).

## Documentation
Start at [docs/README.md](docs/README.md).

## Honest limitations
Kept current as the project grows (policy D25).
- Only M0 and most of M1 exist so far: build system, core utilities, the instruction set as data, the
  reference interpreter, a one-file assembler (no macros, includes or linking until M9) and the
  `asteria` CLI. There are no devices yet (M8), so a program reports its result through registers
  and its exit code, not by printing. Everything else described above is specified in `docs/` and
  scheduled in the roadmap.
- Windows/MSVC only for now, locally and in CI. The code avoids platform-specific APIs outside the GUI, but no
  other compiler has been tested.

## Build (Windows)
Requirements: Visual Studio Build Tools with MSVC 14.5x, CMake ≥ 3.28, Ninja, and vcpkg with
`VCPKG_ROOT` set.
```powershell
git clone https://github.com/DhmalTPS/Asteria-Computer-Studio.git
cd Asteria-Computer-Studio
. .\scripts\dev-shell.ps1        # once per terminal: loads the MSVC environment
cmake --preset debug             # configure (first run builds the test library via vcpkg)
cmake --build --preset debug     # build
ctest --preset debug             # run all tests, including the architecture layering check
.\build\debug\apps\asteria\asteria.exe disasm 000A0044   # -> ADDI R1, R0, 10
```

## Try it
```powershell
$asteria = ".\build\debug\apps\asteria\asteria.exe"
& $asteria run examples\sum.asm              # assemble, run, print the registers; exit code = a0
& $asteria run examples\sum.asm --trace      # one line per instruction
& $asteria build examples\sum.asm            # writes examples\sum.ast
& $asteria disasm examples\sum.ast -o sum.lst.asm   # a listing that reassembles to the same bytes
```
