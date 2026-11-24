# Roadmap

**Current status:** M1 in progress. Parts 1–3 of 4 are done (instruction table, encoder/decoder,
disassembler; reference interpreter with traps, interrupts, privilege and paging; assembler v1, the
AXF `.ast` format, the machine and the `asteria build | run | disasm` CLI). M1 is delivered in four
parts: (1) ISA tables, encode/decode, disassembler; (2) the reference interpreter; (3) the assembler
and the `.ast` executable format; (4) the conformance suite.

**Strategy: build a thin vertical slice first, then deepen it.** After M2 there is always a working,
demoable program, and every later milestone makes an existing path deeper instead of adding disconnected parts.

Each milestone is **done** only when:
1. **Demo:** the listed behaviour runs from the CLI (and from the Studio once M2 exists).
2. **Tests:** unit tests plus the relevant ISA/differential tests pass in CI.
3. **Study note:** `docs/study/Mxx-<name>.md` explains *why* each design choice was made, which
   alternatives were rejected, and where the code lives, and ends with an **interview-style quiz**.
   ★ marks milestones that are interview-core: work through their quiz before moving on.

| # | Milestone | Delivers | Demo |
|---|---|---|---|
| **M0** | Foundation | Repo layout, CMake presets, vcpkg, warnings-as-errors, Catch2, GitHub Actions CI, `common/` | `ctest` green in CI |
| **M1** | ISA core | `isa/` tables, encode/decode, disassembler; **reference interpreter** (all instructions, CSRs, traps, interrupts, privilege, and paging through its own functional page walk; untimed, over flat physical memory); assembler v1 (one file → `.ast`, pseudo-ops, directives); ISA conformance suite | `asteria run hello.asm` prints registers; assemble → disasm → reassemble round-trips |
| **M2** | First Studio | ImGui app: editor, Build, Run/Step on the interpreter, register/memory/disassembly views | **First README GIF** |
| **M3** ★ | Single-cycle CPU | `sim/` scheduler and two-phase commit, `MemPort` with ideal memory; datapath with explicit control signals and per-cycle trace; **differential fuzzer** vs interpreter | Same program, interpreter vs CPU, identical state; control-signal view |
| **M4** ★ | Multi-cycle + Pipeline | FSM CPU (IR/MAR/MDR/A/B/ALUOut); 5-stage pipeline with forwarding, load-use stall, flush, **precise exceptions**, predictors (static, 2-bit, BTB); stage-latency timing model (D8); 3-CPU comparison report | Pipeline diagram view; CPI/T_clk/wall-time table for all three CPUs |
| **M5** ★ | Caches | Configurable cache (mapping, FIFO/LRU/random/PLRU, WT/WB, WA/NWA), L1I/L1D/L2/L3, 3C miss classification, AMAT; benchmark programs | Cache view; matmul (naive vs blocked) miss-rate chart |
| **M6** ★ | Time & DRAM | Femtosecond clock domains (D9); DRAM channels/ranks/banks, row buffers, timing (DDR3/DDR4 presets), refresh, FCFS / FR-FCFS / row-locality schedulers; an **independent timing-legality checker** | DRAM command timeline view; scheduler comparison |
| **M7** ★ | Virtual memory | I/D-TLBs, hardware walker via L2, superpages, ASIDs, A/D bits, page faults on all 3 CPUs | TLB view; TLB miss vs page fault shown side by side |
| **M8** | Devices | Physical bus/router, interrupt controller, timer, keyboard, display console, disk, DMA (with cache probe, D13), NIC (loopback) | Keyboard echo through interrupts; DMA disk read |
| **M9** | Full toolchain | `.aso` objects, linker (multi-file, relocations, `.asa` libraries), AXF segments with permissions, line table; macro assembler; debugger (breakpoints, watchpoints, **reverse step**) | Multi-file program; reverse-stepping a bug |
| **M10** ★ | Asteria C compiler | Lexer, parser, AST, semantic analysis, codegen to Asteria assembly | C program → `.ast` → runs on all 3 CPUs |
| **M11a** ★ | OS core | Boot ROM; trap/syscall entry; processes (`fork` + **COW**, `exec`, `wait`, `exit`, `kill`); context switch; RR; kernel heap; demand paging; drivers; flat-root FS; user libc; minimal shell | Booting to `ASTERIA>`; two programs running concurrently |
| **M11b** ★ | Scheduling & concurrency | Kernel threads; spinlocks (`SWAP`), mutexes, semaphores, condition variables; swappable schedulers (FCFS, SJF/SRTF, priority, MLFQ, lottery, stride); deadlock detector, Banker's lab | **Gantt chart** comparing schedulers; dining philosophers deadlocking, then detected |
| **M11c** ★ | Memory policies | Swap to disk; swappable replacement (FIFO, Clock, aging-LRU, random) vs **Belady OPT**; working set / thrashing; shared memory; OS Studio frame map | Fault-rate vs frame-count curves per policy, with Belady's anomaly shown under FIFO |
| **M11d** ★ | Storage & FS | Mechanical disk model; disk schedulers (FCFS, SSTF, SCAN, C-SCAN, LOOK); directory tree, links, buffer cache, **journaling**, `fsck`; **crash-injection tester**; uid + permissions; optional RAID | Thousands of injected crashes, zero inconsistencies; seek-distance chart per scheduler |
| **M11e** | IPC & shell | Pipes, signals (Ctrl-C → SIGINT, SIGCHLD), full shell (`ls`, `cd`, `run`, `ps`, `kill`, pipelines, `sched`, `vm`, `meminfo`, `cacheinfo`) | `ls \| wc` running, Ctrl-C stopping a looping program |
| **M12** | Release 1.0 | Performance Lab, benchmark report with charts, installer, SDK docs, README polish | Downloadable `AsteriaStudio` installer |

## Level 3 (after 1.0, pick selectively)
Out-of-order core (Tomasulo/ROB, register renaming) · speculative execution · tournament/TAGE
predictors · non-blocking caches with MSHRs, and prefetchers · multicore with MESI coherence and LL/SC ·
Asteria C optimizations (register allocation, SSA).

## Dependency notes
- M1's interpreter implements the **complete** ISA (CSRs, traps, privilege, paging) from the start,
  because it is the oracle every later CPU is checked against.
- Paging is specified in the ISA now, but the timed MMU arrives in M7. Until then the interpreter's
  page walk is the only implementation, and it serves as the oracle for M7.
- The compiler (M10) comes before the OS (M11) by design (D15).
- M11b–M11e can be reordered after M11a. They depend on M11a but not on each other, except that
  M11c's swap uses M11a's disk driver.
- OS scope and its rationale: D22.
