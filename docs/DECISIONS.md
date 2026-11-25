# Architecture Decision Record

Every decision that shapes more than one module is recorded here: **what** was chosen, the
**alternatives** considered, and **why**. When a decision changes, edit its entry and add a
line to its *History*. Never delete an entry silently.

Status values: **Accepted** (in force) · **Proposed** (agreed in principle, not yet exercised by code).

---

## D1. Scope: Layers 5–12, gates are abstract
**Accepted.** We implement from the ISA upward: ISA, three CPU microarchitectures, caches,
virtual memory, DRAM, I/O, OS, and toolchain. Gate-level logic (adders built from NAND, flip-flops)
is *not* simulated. The ALU may use host arithmetic. What we model faithfully is **state, timing and
control**: which register latches what on which cycle, and why.
- Rejected: a gate-level foundation. It multiplies the work and slows the simulator by about 1000×.
  It also teaches nothing about the layers this project exists to show.
- Also rejected (2026-11-18): an optional "Logic Lab" (gate engine, K-map minimizer, control unit
  synthesized from Boolean equations, adder gate depth driving the EX latency). It would have raised
  coverage of the organization course's digital-logic units from ~25% to ~95%, but those units are
  deliberately out of scope.

## D2. Privilege: two modes (User / Kernel), precise exceptions
**Accepted.** One mode bit in `STATUS`. Privileged instructions and CSRs trap in User mode. PTEs
carry a `U` bit. All exceptions are **precise**: older instructions complete, the faulting
instruction and all younger ones have no architectural effect.
- Rejected: no modes (then isolation, `kill` and syscalls are fake). Three modes M/S/U (more trap
  logic with no benefit for one OS on one machine).
- Why now: precise exceptions are the costliest part of the pipeline. Adding privilege later would
  mean re-verifying trap behaviour on three CPUs plus the oracle.

## D3. ISA encoding principles
**Accepted.** See [isa/ASTERIA-32.md](isa/ASTERIA-32.md).
1. `rd`, `rs1` and `rs2` sit at **fixed bit positions** in every format, so registers can be read before the format is known.
2. The immediate's sign bit is **always bit 31**, so sign extension does not depend on the format.
3. **No branch delay slots**, so control hazards stay visible and are handled by the pipeline.
4. The **all-zero word is illegal**, so running into zeroed memory traps immediately.
5. Reserved fields must be zero, otherwise `IllegalInstruction`. Strict decoding makes fuzzing meaningful.

## D4. Arithmetic never traps
**Accepted.** Add/sub wrap. Division by zero gives q = −1 (all ones) and r = dividend.
`INT_MIN / −1` gives q = `INT_MIN` and r = 0.
- Why: arithmetic traps add exception sources in EX for no educational gain.

## D5. Misaligned memory access traps
**Accepted.** No access ever crosses a cache line or a page. That removes split accesses
from the caches, TLB and DRAM.

## D6. TLB refill: hardware page-table walker
**Accepted.** Two-level table (VA = 10 | 10 | 12) with 4 KiB pages and 4 MiB superpages. The hardware
sets the A/D bits. An 8-bit ASID lives in `PTBR`. I-TLB and D-TLB are separate. The walker reads
PTEs **through L2**.
- Rejected: software-managed TLB (MIPS style). It makes a TLB miss an exception, which blurs the
  distinction between a TLB miss and a page fault. It also forces frequent precise traps on all three
  CPUs and mixes handler instructions into user CPI.
- Consequence: a TLB miss is a *stall*, and a page fault is an *exception*. They are distinct by construction.
- The walker also has a **debug mode** that translates a virtual address without writing A/D bits and without
  touching the TLB, stats or timing. The debugger and the Studio use it to show VA→PA. (gem5 does the same.)
- History: 2026-11-18. Debug mode added (references review, P1).

## D7. One memory-port protocol for all requesters
**Accepted.** Every CPU, the walker and DMA talk to memory through the same request/response port.
While a response is outstanding, a CPU **freezes**: no state commits. So the single-cycle CPU
is "1 cycle + memory stall cycles" per instruction.
- Why: caches, TLB and DRAM can then be tested without any CPU, and the three CPUs are compared fairly.
- **Request IDs.** Every request carries an ID, and the response echoes it. Responses are never matched by
  address, because two outstanding requests to the same line would be ambiguous (a bug found in MARSS).
- **Accept/refuse.** A receiver may refuse a request (DRAM queue full, bus busy). The requester stays
  frozen and offers it again on its next clock edge. This is simpler than gem5's retry callbacks and
  compatible with freezing.
- **Debug access.** Every port also offers `debug_read`/`debug_write`: instant and functional. It goes through
  the caches (dirty data wins) and changes **no** state: no LRU update, no stats, no timing. The loader,
  debugger, Studio memory view and differential fuzzer all need it. Without it they would read stale RAM
  behind a write-back cache.
- **Flushes never cancel memory traffic.** A squashed instruction's in-flight request completes and its
  response is discarded. A mispredict must not lose a write-back (a bug found in MARSS).
- **Untimed subset (M1).** The reference interpreter has no time, so it uses `sim::FunctionalMemory`: an
  access happens now or fails now, plus `accepts()` (would it succeed?) so the interpreter can stay precise,
  plus the same `debug_read`/`debug_write`. The timed port arrives with the timed CPUs in M3.
- History: 2026-11-18. IDs, accept/refuse, debug access, and the flush rule added (references review, P1/P3).
  2026-11-24. Untimed `FunctionalMemory` subset added for the M1 interpreter.

## D8. Per-design clock period from a stage-latency table
**Accepted.** T_clk(single-cycle) is the sum of the stage latencies. T_clk(multi-cycle/pipeline) is the
max stage latency plus register overhead. Defaults: IF 200, ID 100, EX 200, MEM 200, WB 100, overhead
20 (ps), which gives 800 ps vs 220 ps. Reports show **cycles, CPI and wall time**.
- Why: if only cycles are counted, single-cycle (CPI = 1) always "wins". That's the wrong lesson,
  because T = N × CPI × T_clk.

## D9. Time: global femtosecond clock + clock domains
**Accepted.** `uint64` femtoseconds, which is exact enough for 833.33 ps DDR clocks and overflows only after
about 5 h of simulated time. Each component is ticked by its domain. The scheduler advances to the
earliest next edge. Memory and device latencies are specified in **ns**. DRAM timing is specified in
**tCK**, as datasheets do. Crossing between domains costs a fixed 2-cycle synchronizer delay.
- Rejected: counting everything in CPU cycles (breaks D8), fixed integer ratios, and full discrete-event
  simulation (overkill).
- **Same-edge order.** Components ticking on the same edge are evaluated in **fixed registration order**,
  which is documented and shown in the Studio. With D10 the order cannot change results, but it keeps traces
  stable. (gem5's equal-time events run LIFO, which is deterministic but confusing.)
- **Rounding warning.** When a configured frequency's period is not a whole number of femtoseconds, the
  simulator reports the rounding error and warns if it exceeds 0.001%.
- **DRAM is cycle-literal.** The controller issues **at most one command per DRAM clock**, so the
  command timeline is real rather than computed analytically in advance (gem5's approach). Each bank tracks
  `open_row` plus the earliest legal time for ACT, RD, WR and PRE. Every command is appended to a **command log**
  that feeds the Studio timeline and an **independent timing checker**. The checker re-derives every
  constraint from the log and never reuses the controller's bookkeeping. gem5 has no such checker.
- History: 2026-11-18. Same-edge order, rounding warning, and the DRAM rules added (references review, P5/P6).

## D10. Two-phase cycle update
**Accepted.** Each cycle, every stateful component first *evaluates* (reads only current state and
writes `next`), then *commits* (`current = next`). Evaluation order can then never leak a value
through two registers in one cycle. This is the classic pipeline-simulator bug.

## D11. Blocking caches
**Accepted.** At most one outstanding miss per cache. MSHRs and non-blocking caches wait for Level 3
(out-of-order or prefetch), where they actually pay off.
- A line being filled is in a **pending-fill** state: its tag is reserved, but its data is not yet valid. DMA probes
  (D13), debug access (D7) and the Studio see the truth. MARSS installs tags at request time, so lines look
  present before their data arrives.
- Replacement policies implement one five-method interface: `reset`, `touch`, `invalidate`, `victim`,
  `make_entry` (gem5's shape). LRU uses a per-set age counter, not timestamps (timestamps tie within a cycle).
- History: 2026-11-18. Pending-fill state and the policy interface added (references review, P4).

## D12. Memory map
**Accepted.** See [platform/MEMORY-MAP.md](platform/MEMORY-MAP.md). Boot ROM at physical 0 (reset PC = 0),
RAM at `0x8000_0000`, MMIO at `0xF000_0000` with one 4 KiB page per device and **always uncached**
(decided by physical address, never by a PTE bit). The user's virtual page range
`0x0000_0000–0x0000_FFFF` is never mapped. The kernel half is identity-mapped (VA = PA).

## D13. Cache coherence with DMA and instruction fetch
**Proposed.** DMA requests **probe** the cache hierarchy. A device→memory write invalidates matching
lines. A memory→device read takes dirty data from the cache. Instruction/data coherence is
**software-managed** through `FENCE.I`, which the loader must execute after writing program text.
- Why: one core means the DMA probe is a few lines of code, and it removes a whole class of OS driver bugs.
  `FENCE.I` stays explicit because self-modifying code is rare, and the I-cache is simpler if it never snoops.

## D14. Determinism is a hard requirement
**Accepted.** Reset zeroes all registers and RAM. The "random" replacement policy uses a seeded PRNG.
External inputs (keyboard, network, disk) are recorded with timestamps. One config plus one input log gives
exactly one execution. This is what makes reverse stepping (checkpoint + re-execute) and bug replay possible.
- **Checkpoints are taken only at cycle boundaries, after `commit()`** (D10). Every component is consistent there,
  so no drain protocol is needed (unlike gem5). Every component serializes **all** of its state, including PRNG
  position, DRAM timing stamps, refresh counters, pending fills and the input-log cursor. The same code serves
  reverse stepping and Studio snapshots (D19).
- **Proof test:** run N cycles, checkpoint, restore, run N more. The full machine state must hash identically to
  running 2N cycles straight. This test runs in CI for every machine configuration.
- History: 2026-11-18. Checkpoint rules added (references review, P7).

## D15. OS implementation language
**Accepted.** Boot code, trap entry and context switch are hand-written in Asteria assembly. Everything else
(scheduler, allocator, filesystem, shell) is written in **Asteria C**, a C subset compiled by our own
compiler. That compiler is built *before* the filesystem and shell.
- Rejected: OS fully in assembly (8–15k lines, hard to maintain). OS in host C++ intercepting `ECALL`
  (it would never run through our caches, pipeline or TLB, which defeats the project).

## D16. Filesystem: inode-based, hierarchical, journaled
**Accepted.** Superblock, block bitmap, inode table (size + 10 direct pointers + 1 indirect),
directories stored as ordinary files (so the tree, path lookup and hard links fall out naturally),
512 B blocks to match the disk's sector size. A **write-back buffer cache** sits above the disk driver.
**Metadata journaling** (write-ahead log, xv6-style) and an `fsck` tool provide crash consistency.
- Built in two steps (M11a flat root directory → M11d full tree + journal), using one on-disk format
  from the start.
- History: 2026-11-18. Upgraded from "flat, Proposed" when the OS scope was widened (D22).

## D17. File types
**Accepted.**
| Ext | Content |
|---|---|
| `.asm` | Assembly source |
| `.ac` | Asteria C source |
| `.aso` | Relocatable object (AXF container, type REL) |
| `.asa` | Static library (archive of `.aso`) |
| `.ast` | Executable (AXF container, type EXEC) |
| `.adisk` | Disk image (AsteriaOS filesystem) |

AXF ("Asteria eXecutable Format") is one ELF-like container used for both objects and executables:
sections for the linker, permission-carrying segments for the loader, symbols, relocations, and a line
table for the debugger.
- The byte layout is specified in [formats/AXF.md](formats/AXF.md). Each executable has exactly one valid
  encoding, and the reader accepts only that one.
- History: 2026-11-25. M1 implements the executable subset (header, segments, symbols); `.aso`,
  relocations and the line table come with the linker in M9.

## D18. Code structure: layered modules, siblings never depend on each other
**Accepted.** See [architecture/OVERVIEW.md](architecture/OVERVIEW.md). Code is organized by **function,
never by layer number**. `cpu`, `mem` and `io` are siblings: none of them includes another. They meet
only through the contracts in `sim` (memory port, IRQ line, clock domain). Only `machine` wires
concrete components together. CMake target dependencies enforce this, so a layering violation is a
build error.

## D19. GUI: Dear ImGui (docking) + ImPlot + DirectX 11
**Accepted.** Chosen for live visualization of large, fast-changing state. The core has **zero GUI
dependencies**: it builds as a DLL for the Studio and links into the `asteria` CLI, so every test runs
headless. The simulator runs on a worker thread and publishes **immutable snapshots** (~60 Hz while
running, and on every step). The GUI never reads live state.
- Rejected: Qt (model/view boilerplate for every live view, LGPL deployment), WinUI/C# (two languages,
  slow large grids), WebView2 (a second language plus a data bridge).

## D20. Language and build
**Accepted.** C++20 baseline plus the C++23 library features MSVC ships (`std::expected`, `std::format`,
`std::print`). MSVC v14.5x, CMake + Ninja, vcpkg manifest mode for third-party libraries (ImGui,
ImPlot, Catch2). Warnings are errors (`/W4 /WX /permissive-`).

## D21. Atomic `SWAP` instruction in the base ISA
**Accepted.** `SWAP rd, rs2, (rs1)` reads and writes one memory word indivisibly.
- Why: without an atomic instruction, user-space locks have to go through a syscall, and the
  test-and-set spinlock lesson disappears. On one core it is one read-modify-write in the MEM stage.
  It is also what multicore (Level 3) needs.
- Rejected for now: LL/SC (only needed with multiple cores and coherence, so it comes with Level 3),
  and fetch-and-add (SWAP alone is enough to build every primitive).

## D22. OS scope: xv6-class kernel + swappable policies + full observability
**Accepted.** AsteriaOS targets the core of a university OS course (~90% of a standard syllabus).
Out of scope: SMP (waits for Level 3 multicore) and hypervisors.

| Area | Included |
|---|---|
| Processes | `fork` with **copy-on-write**, `exec`, `wait`, `exit`, `kill`, process tree, zombies/orphans |
| Threads | Kernel threads; multiple threads sharing one address space |
| Synchronization | Spinlocks (on `SWAP`), sleeping mutexes, semaphores, condition variables; classic problems as demos |
| Deadlock | Wait-for-graph detector in the kernel; Banker's algorithm as an interactive lab |
| CPU scheduling | **Swappable at runtime:** FCFS, SJF/SRTF, priority with aging, RR, MLFQ, lottery, stride |
| Memory | Demand paging, **swappable page replacement** (FIFO, Clock, aging-LRU, random), swap to disk, thrashing / working-set demo, shared memory, kernel and user allocators |
| IPC / signals | Pipes (shell pipelines), signals (SIGINT from Ctrl-C, SIGCHLD, SIGKILL), shared memory |
| Storage | Mechanical disk model (seek + rotational latency); **swappable disk scheduling** (FCFS, SSTF, SCAN, C-SCAN, LOOK); optional RAID 0/1/5 |
| Filesystem | D16 |
| Protection | User IDs, rwx file permissions |

- **Swappable policies:** every scheduling, replacement and disk policy implements one small kernel
  interface and is chosen at runtime (`sched mlfq`, `vm clock`, `disk scan`). Comparing policies is a
  shell command, not a rebuild.
- **Measured against the ideal:** page replacement is compared against **Belady's OPT**, computed
  afterwards from the recorded reference trace.
- **Crash-consistency testing:** thanks to determinism (D14), the test harness can cut power at a random
  cycle, reboot, run `fsck` and check invariants, automatically and thousands of times.
- **OS Studio:** the kernel exports its state (process table, run queues, frame map, swap usage, FS block
  map, wait-for graph) through a read-only debug structure in memory. The Studio reads it from snapshots
  and draws live views and a **scheduling Gantt chart**.

## D23. Instruction semantics are written once
**Accepted.** One decoded-instruction table (generated from the `isa/` tables) and one **semantics layer**:
each instruction's effect is written once, against an abstract execution-context interface (read/write
registers, CSRs, memory, raise trap). The reference interpreter, the single-cycle CPU and the multi-cycle CPU
execute through it. The pipeline keeps its explicit datapath, because Asteria shows control signals, but reuses
the same decode metadata, ALU, comparator and immediate functions.
- **One control word.** ID decodes an instruction into a single control word (RegWrite, ALUSrc, MemRead,
  MemWrite, MemToReg, Branch, Jump, ALUOp, …). It travels down the pipeline with the instruction. No later stage
  re-decodes the opcode.
- Why: LC-John's three models each re-implement instruction behaviour, and they disagree (shift decode,
  undefined-behaviour shifts, divide by zero). MARSS keeps two ISA implementations. With one semantics layer,
  the differential fuzzer tests *microarchitecture*, not three copies of the ISA.
- Forwarding forwards the **write-back value selected by the control word** (PC+4 for JAL/JALR), checks
  `rd ≠ 0`, and has an independent mux per operand. These rules come from bugs found in LC-John.

## D24. Statistics standard
**Accepted.** Every statistic has a hierarchical name (`cpu.stall.dmem`, `l1d.miss.conflict`), a **unit** and a
**description**, and is registered in one line. Derived metrics (CPI, IPC, AMAT, miss rate, row-hit rate)
are **formulas** evaluated on demand, so they can never be stale. Counters support reset (for warm-up) and
latency distributions.
- **Split by privilege level** (User vs Kernel), so kernel overhead and user CPI are separate (MARSS).
- **Stall cycles by cause**, which gives a CPI stack: base + I-fetch + D-memory + data hazard + control hazard +
  structural + TLB walk. This is how "why is the pipeline slower here?" gets answered.

## D25. Credibility policy
**Accepted.** Every claim Asteria makes must be reproducible.
- Every number in the README or docs is **produced by a script** into `results/`, which records the commit
  hash and configuration. Nobody types numbers by hand.
- Every headline claim is **gated by a test** that checks exactly what the claim says (Siliscope's README
  claimed four policies matched, but its test only checked one).
- Incremental commit history, CI that actually runs, no host artifacts in git (logs, transcripts, machine paths).
- The README keeps an **"Honest limitations"** section.
