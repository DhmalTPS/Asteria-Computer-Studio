# Reference review: RISCV-Simulator (LC-John)

**Repo:** github.com/LC-John/RISCV-Simulator · reviewed at commit `29409e1` (2026-11-18) · read only, never built or run.
**Why it's on the list:** the only project found with single-cycle, multi-cycle *and* pipelined modes plus a
cache, which makes it the nearest relative of Asteria's three-CPU design.
**Relevant to:** M3 (single-cycle), M4 (multi-cycle, pipeline), M5 (caches).

## Verdict
A university lab assignment (PKU). It contains **several good mechanisms** and **many instructive bugs**.
Its biggest lesson confirms Asteria's core methodology: the three models were never compared against
each other, and they *disagree* on instruction semantics. Treat it as a catalogue of what Asteria's
shared-semantics layer, D10 and the differential fuzzer prevent.

## What it really is
- C++98-style, Qt `.pro` build, about 5.9k lines. RV64I + most of RV64M (`MULH*` return 0). No ECALL, CSRs or
  exceptions. Hand-written ELF64 loader. The model is selected **at compile time** by `#define`.
- **Verified by nothing:** CI only runs `date`, and there are no test programs or expected outputs. The README's I-cache and
  "dynamic prediction" do not exist in the code.

## The three models as they really are
| Model | Reality |
|---|---|
| "Single-cycle" | A functional interpreter (one big switch, `if.cpp:660`). No cycles, no CPI, **shares no execution code** with the others |
| Multi-cycle | **No FSM** and no IR/MDR/A/B registers. Runs a whole instruction, then *adds up* fixed per-unit costs (branch 3, store 4, ALU 4, load 5, DIV 40). The EX block is copied four times |
| Pipeline | Each stage object is its own latch. Each cycle: shift latches in reverse order, evaluate stages, then run hazard logic that **mutates ID after evaluation**. Only the register file is truly two-phase (`reg_def.h:25-34`) |

Control signals have CS61C-style names (`ImmSel`, `ASel`, `BSel`, `ALUSel`, `PCSel`, `MemRW`, `WBSel`,
`RegWEn`, `reg_def.h:60-144`), but **each unit re-decodes them from the opcode**. No control word travels with
the instruction.

## Adopt in Asteria
| Idea | Where | Milestone |
|---|---|---|
| **Generic mispredict check:** EX's computed next-PC ≠ PC of the instruction now in ID, so flush. It works unchanged for static, 2-bit and BTB predictors | `pipeline_sim.cpp:451` | M4 |
| Per-stage **stall bits + bubble bits**, with the bubble inserted at the first unstalled stage. Clean data for the pipeline diagram view | `pipeline_sim.cpp:689-708` | M4 |
| Each stall cause as its own small detector with its own counter, so CPI can be broken down by cause | `multicycle_sim.cpp:138-147` | M4 |
| MEM stage describes its request as {address, size, read/write}, independent of the cache behind it | `mem.cpp:181-200` | M3 (D7) |
| `Storage` interface with a `lower_` pointer forming the hierarchy chain | `storage.h`, `cache.h:22` | M5 |
| CS61C control-signal names, **as one control word produced in ID and carried down the pipeline** | `reg_def.h:60-144` | M3/M4 |

## Avoid: bugs found by reading (and what prevents them in Asteria)
| Bug | Where | Asteria's prevention |
|---|---|---|
| **Lost redirect:** a wrong-path branch in IF overwrites a mispredict redirect (no check that IF is being squashed) | `pipeline_sim.cpp:139-140, 481-505` | D10 + one redirect priority rule; fuzzer |
| **JAL/JALR forward the jump target instead of PC+4** | `pipeline_sim.cpp:539-558` | Forward the *writeback value* selected by the control word, not the ALU output |
| **No `rd ≠ 0` guard** in forwarding | `pipeline_sim.cpp:512-606` | Forwarding conditions include `rd != 0` (spec §1) |
| **When rs1 = rs2, only one operand is forwarded** (`if … else if`) | `pipeline_sim.cpp:522-560` | Each operand has its own forwarding mux |
| Dead EX2 path makes **DIV/REM retire about 39 times**, inflating instruction counts and improving CPI | `pipeline_sim.cpp:24, 100, 697-704` | Retirement counted once per instruction ID; INSTRET is compared by the fuzzer |
| **Cache LRU loop bounded by `line_size`** instead of lines per set, writing out of bounds | `cache.cpp:152, 230, 250` | `ASTERIA_ASSERT` on indices; exhaustive cache unit tests |
| **Halt address never 4-aligned**, so the run ends as "invalid PC" before stats are printed | `elf_reader.cpp:524`, `pipeline_sim.cpp:721` | Explicit `HALT` with exit code (spec §4.7) |
| Models disagree on semantics (funct3 vs funct7 decode; unmasked 64-bit shifts are UB; divide by zero crashes the host) | `if.cpp:938`, `ex.cpp:243, 356-376` | **One shared semantics layer** plus spec-defined division (D4) plus the differential fuzzer |
| **Unfair comparison:** only the pipeline sees cache latency, multi-cycle CPI is a fixed table, there is no clock period, and "ns" are used as cycles | `storage.h:13`, `pipeline_sim.cpp:233` | D7 (all CPUs share the same memory) + D8 (per-design T_clk) + `SimTime` |
| ELF opened in text mode (corrupts binaries on Windows), no bounds checks | `elf_reader.cpp:23`, `mem.cpp:122-171` | Binary I/O, `Result<T>` on malformed files |
