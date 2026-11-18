# Asteria Computer Studio

A complete computer, designed and built in software: an original 32-bit instruction set (**ASTERIA-32**),
three processor microarchitectures that run it (single-cycle, multi-cycle, 5-stage pipeline), a
cycle-aware memory system (caches, MMU/TLB, DRAM controller with real timing), interrupt-driven devices
with DMA, a self-written toolchain (assembler, linker, C compiler, debugger), and **AsteriaOS**, which runs
on the virtual machine. All of it lives in one Windows desktop application where you can see every cycle.

> Status: **M0 complete** (build system, architecture checks, core utilities, CI). Next: M1, the
> ISA core. See [docs/ROADMAP.md](docs/ROADMAP.md).

No existing emulator, assembler, cache or memory simulator is used. Third-party code is limited to
the GUI (Dear ImGui, ImPlot) and the test framework (Catch2).

## Documentation
Start at [docs/README.md](docs/README.md).

## Honest limitations
Kept current as the project grows (policy D25).
- Only milestone M0 exists so far: build system, architecture checks, core utilities. **No part of the machine
  runs yet.** Everything described above is specified in `docs/` and scheduled in the roadmap.
- Windows/MSVC only for now. The code avoids platform-specific APIs outside the GUI, but no other compiler has
  been tested.
- The CI workflow is written but has not run yet, because the repository has no remote.

## Build (Windows)
Requirements: Visual Studio Build Tools with MSVC 14.5x, CMake ≥ 3.28, Ninja, and vcpkg with
`VCPKG_ROOT` set.
```powershell
. .\scripts\dev-shell.ps1        # once per terminal: loads the MSVC environment
cmake --preset debug             # configure (first run builds the test library via vcpkg)
cmake --build --preset debug     # build
ctest --preset debug             # run all tests, including the architecture layering check
.\build\debug\apps\asteria\asteria.exe help
```
