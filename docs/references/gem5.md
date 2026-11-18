# Reference review: gem5

**Repo:** github.com/gem5/gem5 · reviewed at commit `9d36889` (2026-11-18) · read only, never built or run.
Only the infrastructure was studied: `src/sim`, `src/mem`, `src/cpu/minor`, `src/cpu/simple`, `src/mem/cache`,
DRAM interface/controller, statistics, and the RISC-V page-table walker.
**Why it's on the list:** the canonical research simulator. Asteria learns from its *infrastructure
design*, not its scale.
**Relevant to:** M3–M9 (time, ports, CPUs, caches, DRAM, MMU, stats, checkpoints).

## Verdict
gem5 independently confirms most of Asteria's core decisions: a 64-bit integer time base, strong
cycle types, clock domains, a port protocol, and a pluggable replacement policy. It also contributes **six concrete
improvements** (listed at the end). Much of what makes gem5 big is overkill for one in-order core with
blocking caches, and is explicitly avoided below.

## 1. Time and events. Validates D9
- `Tick` is a `uint64` at 1 ps by default (`sim/core.cc:79`). gem5 **warns when a clock period rounds by more than
  0.1%** (`python/m5/ticks.py:58-90`). Asteria's femtoseconds give 1000× finer resolution (DDR4-2400:
  833'333 fs, error 0.00004%) in exchange for ~5 h instead of ~213 days of simulated time before overflow.
- `Cycles` is a separate type from `Tick` (`base/types.hh:78`), the same idea as `SimTime` vs cycle counts.
  `ticksToCycles` rounds **up** (`sim/clocked_object.hh:222`), like our `cycles_to_cover`.
- Clock domains with dividers (`sim/clock_domain.hh`) correspond to Asteria's clock domains.
- The event queue orders by (time, priority) and is **LIFO among ties** (`sim/eventq.hh:259-268`): deterministic
  but non-obvious. Asteria's scheduler only has clock edges, so components ticking on the same edge run
  **in fixed registration order**, and that order is documented.

## 2. Ports and packets. Refines D7
- Every port supports three access modes (`mem/port.hh`): **timing** (`sendTimingReq` may be refused, then retry),
  **atomic** (latency returned in one call), and **functional** (instant, no state change, sees dirty data in caches).
- `PortProxy` (`mem/port_proxy.hh:86`) wraps functional access for the loader, syscall emulation and GDB.
- **Adopt:**
  - A **functional debug path** on `MemPort` (`debug_read`/`debug_write`): traverse the caches with dirty lines
    winning, without touching LRU, stats or timing. Without it, the M9 debugger and the Studio memory view read
    stale RAM behind a write-back L1-D, and the M3 fuzzer cannot compare memory.
  - A **one-bit accept/ready** on requests (full DRAM queue, busy bus) with retry on the next edge. That is simpler
    than gem5's retry callbacks and compatible with "requester freezes".
- **Skip:** atomic mode as a separate protocol (M3's ideal memory plays that role), snoop ports, crossbar delays,
  and `senderState` stacks.

## 3. CPU models. Validates D10
- The in-order Minor CPU evaluates stages **in reverse order** (execute → decode → fetch2 → fetch1,
  `cpu/minor/pipeline.cc:133-136`), so that zero-delay backward signals (stall, branch redirect) are seen within
  the same cycle. Forward data passes through `Latch`es with a declared delay (`cpu/minor/buffers.hh:221-283`).
  - Asteria's D10 (evaluate all, then commit) removes the ordering hazard by construction. **Keep D10.** Model
    same-cycle combinational signals (hazard unit, forwarding muxes) as functions of *current* pipeline-register
    state, never as one stage writing into another mid-cycle.
- **Semantics separated from timing:** `StaticInst::execute()` holds an instruction's complete meaning
  (`cpu/static_inst.hh:294`). Memory instructions split into `initiateAcc` and `completeAcc`
  (`cpu/simple/timing.cc:859, 1056`), all against an abstract `ExecContext` (`cpu/exec_context.hh:71`).
  - **Adopt:** one decoded-instruction table plus a shared semantics layer over an `ExecContext`-like interface.
    The interpreter and the single-/multi-cycle CPUs share it, so semantics are written once. The pipeline keeps
    its explicit datapath (Asteria shows control signals) but reuses decode metadata and the ALU and branch functions.

## 4. Caches. Shapes M5
- Cache → `tags` (`findBlock`/`findVictim`/`insertBlock`, `mem/cache/tags/base.hh:202-334`) → indexing policy +
  **replacement policy with only five methods**: `invalidate`, `touch`, `reset`, `getVictim`,
  `instantiateEntry` (`mem/cache/replacement_policies/base.hh:66-111`). Random uses a seeded RNG and prefers
  invalid ways.
- Latency is split into tag, data and response, plus a `sequential_access` flag (tag lookup then data vs in
  parallel) (`mem/cache/Cache.py:89-131`). That makes a good teaching knob.
- **Adopt:** the five-method replacement interface and the latency split. **Differ:** LRU uses a per-set
  counter, not a timestamp (ticks tie within a cycle and mean different things across clock domains).
- **Skip:** MSHRs, write buffers, clusivity, compression, partitioning, prefetchers (D11).

## 5. DRAM. Shapes M6
- Each bank holds `openRow` plus four "earliest legal time" stamps: `rdAllowedAt`, `wrAllowedAt`, `preAllowedAt`,
  `actAllowedAt` (`mem/mem_interface.hh:98-105`). Issuing a command pushes the affected stamps forward (tRCD,
  tRAS, tRRD, the tFAW window, read↔write turnaround) (`mem/dram_interface.cc:176-235, 285-326`).
- FR-FCFS (`dram_interface.cc:61-170`): seamless row hit, then prepared row hit, then the earliest bank.
  Writes drain by high/low watermarks. Page policies: open, close, and adaptive variants.
- Every command goes into a per-rank command list.
- **Adopt:** per-bank `openRow` + `*AllowedAt` state (compact, displayable, and it answers "can I issue X now?"),
  FCFS/FR-FCFS behind one `choose_next`, a page-policy enum, write watermarks, and a **command log** that feeds the
  Studio timeline.
- **Differ:**
  - Asteria issues **one command per DRAM clock**, so the timeline is literal. gem5 computes future issue times
    analytically.
  - Timing parameters are in tCK as datasheets give them (D9). gem5 uses ns.
- **Original work:** gem5 has **no independent timing-legality checker** (`mem_checker` checks data values).
  Asteria's M6 checker re-derives every constraint from the command log, independently of the controller's
  `*AllowedAt` bookkeeping.

## 6. Statistics, configuration, checkpoints
- **Stats:** `Scalar`/`Vector`/`Distribution`/`Histogram` and lazily evaluated **`Formula`**, grouped
  hierarchically. `ADD_STAT(name, unit, description)` registers everything in one line (`base/stats/group.hh:75`).
  - **Adopt for `StatsRegistry`:** hierarchical names (`l1d.misses`), mandatory unit and description, formulas for
    derived metrics (CPI, AMAT, row-hit rate) so they are never stale, `reset` for warm-up, distributions for latency.
- **Config:** Python `SimObject` params with defaults and descriptions.
  - **Adopt** the *idea* of one declarative, self-documenting parameter schema per component, so the Studio can
    render and validate config panels. Implement it as C++ structs plus TOML/JSON. **Skip** embedded Python.
- **Checkpoints:** `serialize`/`unserialize` per object (`sim/serialize.hh:169-251`). Every object must first
  **drain** to a quiescent state (`sim/drain.hh:75-81`).
  - **Asteria needs no drain:** after `commit()` every component is at a consistent cycle boundary (D10).
    Every component serializes *all* its state (including PRNG position, DRAM stamps, refresh counters, input-log
    cursor). The same code serves reverse step (D14) and GUI snapshots (D19).
  - **Test:** run N cycles, then checkpoint, restore and run N more. The result must be hash-identical to 2N straight.
- **MMU:** the RISC-V walker's functional mode skips the A/D write-back (`arch/riscv/pagetable_walker.cc:698-704`).
  That gives the debugger VA→PA translation without side effects. **Adopt** for M7.

## Six improvements this review contributes to Asteria
1. **Functional debug access** on `MemPort` (debugger, loader, memory view, fuzzer). Also needs a walker debug mode.
2. **Shared instruction semantics** (`ExecContext`-style), written once and used by the interpreter and CPUs.
3. **Five-method replacement-policy interface**, LRU by per-set counter.
4. **DRAM per-bank `*AllowedAt` + command log**, with the legality checker as original work.
5. **Stats with units, descriptions and formulas**, with hierarchical names.
6. **Serialize/restore at commit boundaries**, with a hash-identical round-trip test, plus a warning when clock
   periods round.
