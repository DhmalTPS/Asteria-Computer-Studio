# Architecture Overview

This document shows how the code is organized and *why*. Read it before opening any source file.

## 1. Repository layout

```
App_AsteriaStudio/
├── CMakeLists.txt            Root build: options, warnings, adds src/ apps/ tests/
├── CMakePresets.json         One-command configure/build: debug, release, ci
├── vcpkg.json                Third-party libraries (ImGui, ImPlot, Catch2), and nothing else
├── README.md                 What it is, screenshots, quick start
├── .github/workflows/        CI: configure, build and test on every push
├── cmake/                    Build helpers: compiler options, asteria_add_module(), layering checker
├── scripts/                  Developer scripts (dev-shell.ps1 loads the MSVC environment)
│
├── docs/                     Specifications: the source of truth that code must obey
│   ├── README.md             Reading order
│   ├── DECISIONS.md          Every cross-cutting decision with its rationale
│   ├── ROADMAP.md            Milestones, each ending in a demo and a study checkpoint
│   ├── architecture/         This file (modules, dependencies, data flow) + CODING-STYLE.md
│   ├── isa/                  ASTERIA-32 instruction set specification
│   ├── platform/             Memory map, devices, boot
│   ├── formats/              AXF object/executable format, disk image format
│   ├── references/           Reviews of external projects: what to adopt, what to avoid
│   └── study/                One study note per milestone: design walkthrough + interview quiz
│
├── src/                      The simulator and tools (C++). One folder = one CMake library
│   ├── common/               Bit utilities, fixed-width types, Result/error, femtosecond time
│   ├── isa/                  ISA tables (single source of truth), encode, decode, disassemble
│   ├── sim/                  Simulation kernel + the CONTRACTS siblings meet through:
│   │                         clock domains, scheduler, memory port, IRQ line, stats, trace
│   ├── cpu/                  Processor models
│   │   ├── core/             Shared parts: register file, ALU, CSR file, trap unit, control signals
│   │   ├── interp/           Reference interpreter: the correctness oracle (no timing)
│   │   ├── single_cycle/
│   │   ├── multi_cycle/
│   │   └── pipeline/         + predict/ (static, 2-bit, BTB)
│   ├── mem/                  Memory hierarchy
│   │   ├── phys/             Physical bus/router, RAM, ROM
│   │   ├── cache/            Configurable cache, policies, 3C miss classifier
│   │   ├── mmu/              TLB, page-table walker, permission checks
│   │   └── dram/             Controller, scheduler (FCFS, FR-FCFS), bank state machines, timing presets
│   ├── io/                   Devices
│   │   ├── intc/             Interrupt controller
│   │   ├── dma/              DMA engine
│   │   └── devices/          timer, keyboard, display, disk, nic
│   ├── machine/              The ONLY place that wires concrete components into a computer
│   ├── toolchain/            Programs that build programs
│   │   ├── axf/              AXF read/write (shared by assembler, linker, loader, debugger)
│   │   ├── as/               Assembler: lexer, parser, macro expander, encoder
│   │   ├── ld/               Linker
│   │   └── cc/               Asteria C compiler (milestone M10)
│   └── studio/               GUI only: app shell, editor, views/
│
├── apps/
│   ├── asteria/              CLI:  asteria build | run | debug | disasm | profile | bench
│   └── studio/               AsteriaStudio.exe entry point (thin, calls src/studio)
│
├── os/                       AsteriaOS: code that runs ON the virtual machine (asm + Asteria C)
│   ├── boot/                 Boot ROM image
│   ├── kernel/               Traps, syscalls, processes, scheduler, VM, allocator
│   ├── drivers/
│   ├── fs/
│   ├── lib/                  User-space runtime (libc subset)
│   └── user/                 Shell and user programs
│
├── examples/                 Small, commented assembly programs for learning
│
└── tests/                    Mirrors src/ exactly, so the test for src/X/Y lives in tests/unit/X/
    ├── unit/<module>/        Catch2 unit tests per module
    ├── isa/                  Self-checking .asm conformance tests (HALT with a0 = 0 means pass)
    ├── cli/                  The asteria CLI's contract: exit codes and messages
    ├── diff/                 Differential fuzzer: interpreter vs each CPU
    └── bench/                Benchmark programs (matmul, sort, sequential, random)
```

### Layout rules
1. **One folder = one concept = one CMake target.** If you can't name a folder's single responsibility in
   one line, it is split wrong. A module with subfolders (`cpu/`, `mem/`, `io/`, `toolchain/`) gets one target
   per **leaf** folder, named after its path: `src/cpu/core/` is `asteria::cpu_core`, `src/mem/phys/` is
   `asteria::mem_phys`. The dependency table in section 2 applies to the top-level module.
2. **Headers live next to their `.cpp`.** Include paths start at `src/`: `#include "mem/cache/cache.hpp"`.
   The path tells you the module.
3. **Namespaces mirror folders:** `asteria::mem::cache`.
4. **Tests mirror sources:** `src/mem/cache/` → `tests/unit/mem/cache/`.
5. **No `utils/`, `helpers/` or `misc/` folders.** Generic code goes in `common/` only if two or more modules need it.
6. **Folders named by function, never by layer number** (D18).

## 2. Module dependency graph

Arrows mean "depends on". Anything not drawn is **forbidden**, and CMake enforces it.

```
                 studio (GUI)        apps/asteria (CLI)
                      \                 /
                       ▼               ▼
                          machine  ─────────────┐
                    ┌────────┼────────┐         │
                    ▼        ▼        ▼         ▼
                   cpu      mem       io     toolchain
                    │ \      │       /│         │
                    │  └──►  sim  ◄──┘│         │
                    ▼        │        │         │
                   isa ◄─────┼────────┼─────────┘
                    │        ▼        │
                    └───► common ◄────┘
```

The same graph as an exact table (the source of truth for `target_link_libraries`):

| Module | May depend on |
|---|---|
| `common` | (nothing) |
| `isa` | common |
| `sim` | common |
| `cpu` | isa, sim, common |
| `mem` | sim, common |
| `io` | sim, common |
| `toolchain` | isa, common |
| `machine` | cpu, mem, io, toolchain (its loader), sim, isa, common |
| `studio`, `apps/*` | machine, toolchain, and everything below |

- **`cpu`, `mem` and `io` never include each other.** A CPU knows only a `sim::MemPort`. It cannot tell a
  flat RAM from a 3-level cache with DRAM behind it. A device raises a `sim::IrqLine`. It doesn't know
  which CPU is listening.
- **Why this matters:** every component can be unit-tested alone (a cache against a fake memory, a CPU
  against an ideal memory), and the same CPU runs against any memory system unchanged.
- `toolchain` never depends on the simulator. The assembler would work without any machine existing.

## 3. The contracts (defined in `src/sim/`)

| Contract | Purpose | Implemented by | Used by |
|---|---|---|---|
| `ClockDomain` / `Clocked` | `evaluate()` then `commit()` on each edge (D10) | every stateful component | scheduler |
| `FunctionalMemory` | untimed subset of `MemPort` for the M1 interpreter: access now or fail now, `accepts()` to check first, debug access (D7) | `mem/phys` bus | reference interpreter, loader |
| `MemPort` | request (with ID) → may be refused → response echoing the ID after N cycles; requester stalls. Plus side-effect-free `debug_read`/`debug_write` (D7) | RAM, caches, MMU, bus, DRAM controller | CPUs, walker, DMA, caches (downstream), loader, debugger |
| `IrqLine` | level-sensitive interrupt wire | interrupt controller input | devices |
| `Probe` | cache probe for DMA coherence (D13) | caches | DMA |
| `StatsRegistry` | hierarchical counters with units, formulas, per-privilege split, stall causes (D24) | everyone | profiler, Studio, reports |
| `TraceSink` | per-cycle events, compiled cheaply when off | everyone | debugger, Studio, trace export |

## 4. One load, end to end

`LW R7, 0(R6)` on the pipeline with the full hierarchy:

```
IF   I-TLB hit ──► L1-I hit ──► instruction in IF/ID
ID   decode → control signals; read R6; hazard unit checks for a load-use hazard
EX   ALU: R6 + sext(0) = VA
MEM  D-TLB lookup ── miss ──► walker reads PTE via L2 (may stall)
                     hit  ──► PA ──► L1-D ── miss ──► L2 ── miss ──► DRAM controller
                                                         ACTIVATE row → READ (tRCD, tCL) → burst
     ◄──────────────────────── line fill L2 → L1-D ◄──── data ◄───────┘
     (pipeline frozen behind MEM the whole time: D7)
WB   R7 ← data
```

Every arrow above is visible in the Studio, and each one shows up in `StatsRegistry`.
