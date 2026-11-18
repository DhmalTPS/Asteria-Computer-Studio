# Reference review: MARSS-RISCV

**Repo:** github.com/bucaps/marss-riscv · reviewed at commit `82b3ce2` (2026-11-18) · read only, never built or run.
Studied: the timing code under `src/riscvsim/` (core, memory hierarchy, DRAM, utils), not TinyEMU's functional emulator.
**Why it's on the list:** a research full-system cycle-level simulator: in-order and out-of-order cores,
caches, TLB, DRAM, and it boots Linux.
**Relevant to:** M3–M7 (pipeline, caches, DRAM, MMU), stats, Studio.

## Verdict
Strong as a *full-system* reference and full of practical machinery (latch pools, stall-cause
accounting, per-privilege stats). But its timing is **bolted onto a functional emulator** (TinyEMU). That
causes the main pitfalls: two ISA implementations, synchronous latency calculations, one DRAM request
at a time, a single clock, and nondeterminism. Asteria's D7/D9/D10/D14 avoid each of these by design.

## What it really is
- TinyEMU provides the interpreter, devices, the software TLB and the functional page walk. MARSS adds about 17k lines in
  `riscvsim/`:
  - in-order and OoO cores
  - its **own second** decoder and executor
  - tag-only caches
  - memory controller + simple DRAM + DRAMSim3/Ramulator wrappers
  - branch predictors
  - JSON params, stats
- A CSR write switches between emulation and simulation. The pipeline **drains and hands back to TinyEMU on every
  SYSTEM/CSR instruction, illegal opcode or MMU fault**. SYSTEM instructions cost a flat latency.

## Design in brief
| Area | How it works |
|---|---|
| Stages | `CPUStage{has_data, insn_latch_index}`. Stages pass an **index into a pool of instruction latches**, and each latch counts `elapsed/max` cycles for any multi-cycle wait |
| Order | Stages evaluated **in reverse order** instead of two-phase. The 5-stage variant depends on calling pcgen *before* fetch, so **pipeline depth depends on call order** (`inorder.c:247-287`) |
| Hazards | Scoreboard + forwarding buses. A sequence-number queue keeps variable-latency units completing in order (`inorder_backend.c:116-132`) |
| Branches | Resolved in **MEM**, plus a skip-fetch bubble |
| Caches | Tag-only (data stays in emulator RAM). WB/WT, allocate policies, PLRU/random. Latency *returned synchronously*, and tags updated at request time |
| DRAM | **One request in flight.** Built-in model: fixed latency × 0.6 for same-page. DRAMSim3/Ramulator **clocked in a loop until done** per request |
| TLB/walk | TinyEMU's direct-mapped software TLB (0-cycle hit, no ASID, flush-all). Walk PTE reads charged through L1-D into a global `page_walk_delay` |
| Clocking | One CPU clock. DRAM cycles counted as CPU cycles. Interrupts sampled every 500k instructions. Devices and DMA untimed |
| Stats | Flat struct, **split per privilege level**, stall cycles by cause, CSV dump, live shared-memory mirror |

## Adopt in Asteria
| Idea | Where | Milestone |
|---|---|---|
| **Instruction-latch pool**: stages hold an index, and one record per in-flight instruction feeds the pipeline view | `inorder_frontend.c:66-78`, `cpu_latches.h` | M4 |
| Uniform "elapsed/max cycles" counter for any multi-cycle wait | `cpu_latches.h` | M4 |
| In-order completion queue for multi-cycle MUL/DIV | `inorder_backend.c:116-132` | M4 |
| **Stall cycles by cause** (I-mem, D-mem, functional unit, …), giving a CPI stack | `utils/sim_stats.h` | M4 |
| **Stats split by privilege level** (separates kernel CPI from user CPI) | `utils/sim_stats.h` | M7/M11 |
| Policy slots chosen at init (write handler, victim, allocate) | `cache.c:540-587` | M5 |
| Fixed-latency + row-hit-discount DRAM as a **baseline** to compare against the bank model | `dram.c:74-95` | M6 |
| Walk counters per access type (fetch, load, store), TLB lookup and hit counters | `riscv_cpu.c:257-269, 638-650` | M7 |
| Make the branch resolution stage an explicit, configurable choice (EX vs MEM) | `inorder_backend.c:366-371` | M4 |

## Avoid (and Asteria's answer)
| Pitfall | Where | Asteria's answer |
|---|---|---|
| Correctness depends on stage call order | `inorder.c:247-287` | D10 evaluate → commit |
| Stall = poll a shared counter matched **by address** (two requests to one line can match the wrong entry) | `dram.c:156-211` | Port responses matched by **request ID** (D7 amendment) |
| **A mispredict flush resets the memory controller and DRAM**, discarding in-flight writebacks | `inorder_backend.c:318` | Flushes never touch the memory system. Squashed requests complete and are discarded |
| Cache tags installed at request time, before data arrives | `cache.c:98-99` | Line state includes **pending fill**, visible to DMA probes and the Studio |
| DRAM run to completion per request, so idle DRAM time doesn't advance (refresh wrong), and bank parallelism and FR-FCFS can't occur | `dramsim_wrapper.cpp:94-120`, `dram.c:214` | DRAM is its own clock domain (D9) with a real queue |
| Probable 8× over-fetch per line; converted DRAM address computed but not used | `dramsim_wrapper.cpp:105-111`, `dram.c:123-124` | Burst = line by construction; unit tests on the address mapping |
| Two ISA implementations; SYSTEM instructions bounce out to the emulator | `riscv_cpu_template.h:265-344` | One ISA table + shared semantics; every CPU executes every instruction, including traps |
| TLB = emulator's 0-cycle accelerator, no ASID | `riscv_cpu.c:622-626, 687-689` | Real I/D TLBs with ASIDs (D6); miss = port stall |
| Interrupts every 500k instructions; untimed devices and DMA | `temu.c:536` | Interrupts checked at every instruction boundary; devices clocked (M8) |
| Unseeded `rand()` eviction | `evict_policy.c:91` | Seeded PRNG (D14) |
