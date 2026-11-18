# Reference review: Siliscope

**Repo:** github.com/VIKASNARASIMHA1/Siliscope · reviewed at commit `76fc0d8` (2026-11-18) · read only, never built or run.
**Why it's on the list:** its README claims an emulator, microarchitecture analyzer and OS with very
broad scope. Earlier research flagged the claims as needing verification.
**Relevant to:** M7, M9, M11a–e, testing methodology, and **how Asteria presents itself**.

## Verdict: the claims are real
**The suspicion was wrong.** Nearly every README claim is backed by working, dense code: about 13k lines
(Rust emulator ~3.1k, analyzer ~3.0k, C kernel ~2.6k, user programs ~1.5k, build/test driver ~2.2k), with real
tests and no placeholder code. It is effectively a **compressed xv6 on a QEMU-`virt`-compatible RISC-V
machine plus a trace-driven performance analyzer**.

**This is Asteria's closest competitor**, and it already has much of Asteria's OS scope: COW fork,
demand paging, swap with FIFO/Clock/aging/random vs OPT, a journaled FS with crash injection and fsck, pipes,
MLFQ, and a GDB stub. Asteria must therefore be clear about what it does that Siliscope does not (see the last section).

## Claim verification (selected)
| Claim | Status | Evidence |
|---|---|---|
| RV64IMAC, M/S/U modes, trap delegation; 110/110 riscv-tests | real | `kernel/start.c:30-37`, `emulator/src/cpu.rs:99-160`, `tests/riscv-tests/` |
| Sv39, ASID-tagged TLB, superpage TLB, hardware A/D | real | `mmu.rs:45-75, 141-233` |
| CLINT, PLIC, UART, virtio-blk (interrupt-driven) | real | `devices/*.rs`, `kernel/virtio.c` |
| Caches (5 policies), prefetchers, DRAM, predictors, 5-stage and OoO models | real, but **trace-driven** and **formula-style** (README says so) | `perf/*.rs` |
| Preemptive multitasking, RR/MLFQ | real for **user mode only** (kernel non-preemptible) | `trap.c:70-79`, `proc.c:137-178` |
| COW fork, lazy `sbrk`, swap with 4 policies | real; swap covers **heap pages only** | `vm.c:221-270`, `swap.c:108-171` |
| "Kernel matches textbook simulators exactly, all four policies" | **partial**: the test gate only checks FIFO | `xtask/src/main.rs:360-362` |
| Journaled FS + crash injection + host fsck | real (xv6-style log, no-journal control build) | `log.c:69-131`, `virtio_blk.rs:184`, `xtask/src/fsck.rs` |
| GDB remote stub | real | `gdb.rs:233-425` |
| SMP | real but simple: lockstep harts, big kernel lock, no TLB shootdown | `machine.rs`, `spinlock.c` |
| CI on Linux and Windows on every push | **contradicted** by its own "Honest limitations" section | `ci.yml` vs README |
| Threads, sync primitives, signals, users and permissions | not present (never claimed) | – |

## The key architectural difference: timing never affects behaviour
The analyzer is a **passive consumer** of the retired-instruction stream (`cpu.rs:139-143`). The OS timer is
**instruction count ÷ 10** (`cpu.rs:12-13`). Caches, pipelines and DRAM therefore **cannot change** what the OS does:
when the scheduler preempts, how many page faults occur, or results that depend on timing. Page-walk memory references
aren't in the trace, so TLB misses cost nothing.

In Asteria the CPUs *are* the timing models. A cache miss stalls the real machine, the timer runs on real
simulated time (D9), and so the **memory system changes scheduling and OS behaviour**. This is Asteria's
clearest technical differentiator.

## Adopt in Asteria
| Idea | Where | Milestone |
|---|---|---|
| **Crash tester:** kill switch after N disk writes; workload prints completion markers; host fsck derives the expected state; **no-journal control build** ("51 of 69 crash points corrupt without the log") | `virtio_blk.rs:184`, `fsck.rs:234-253`, `log.c:126` | M11d (Asteria adds cycle-exact cuts and torn writes) |
| **Belady check:** guest prints its exact reference string, host replays it through offline FIFO/Clock/aging/random/OPT and asserts kernel = simulator. **Gate every policy, not just FIFO** | `swapbench.c:43`, `xtask/src/paging.rs:19-148` | M11c |
| Memory balloon (shrink usable RAM at runtime) for thrashing demos; reclaim with low/high watermarks | `vm.c:26-52`, `swap.c:19-20` | M11c |
| `SCRATCH == 0` tells a kernel trap from a user trap, so one trap vector handles both | `trapvec.S:7-8` | M11a (our `SCRATCH` CSR) |
| Kernel stack canary checked on every switch and return to user | `proc.c:198`, `trap.c:86` | M11a |
| `copyout` must break COW and fault in lazy or swapped pages | `vm.c:280-301` | M11a/c |
| MLFQ table: slice per level, periodic boost, preempt on higher-priority wakeup | `trap.c:7-16, 70-79` | M11b |
| Test harness: guest prints `RESULT key=value`, host parses; in-guest `ktest` with `CHECK`; unit tests pin exact cycle counts ("load-use = +1"); monotonic invariants ("more resources never slower") | `xtask/src/main.rs:263`, `perf/ooo.rs:632-660` | M3/M4, M11 |
| GDB remote-protocol stub with a raw-socket smoke test | `gdb.rs`, `xtask/src/main.rs:768-815` | M9 (optional) |

## Avoid
- Swap limited to heap pages, and fork swapping in the whole parent: design swap for all user pages.
- Byte-wide page reference counts (overflow at 255 sharers).
- A non-preemptible kernel limits scheduling realism. Decide Asteria's kernel preemption model explicitly in M11b.
- A test gate that checks less than the README claims.

## Credibility lessons (for Asteria's README and resume)
What weakens Siliscope's presentation:
- the history looks like one squashed commit
- committed raw terminal transcripts containing the author's machine paths
- a placeholder clone URL
- a 905-line README of "Phase N" sections
- headline claims contradicted by its own limitations section

What makes it credible anyway: exact numbers backed by committed CSVs and plots, the tests that generate them,
a control experiment, and a candid "Honest limitations" section.

**Asteria's rules (proposed D26):** incremental commit history; a CI badge that really runs; every README number
generated by a script into `results/` with the commit hash; every headline claim gated by a test; no host
artifacts in git; an honest-limitations section.

## How Asteria differs (positioning)
| | Siliscope | Asteria |
|---|---|---|
| ISA | RISC-V (existing) | **Original ASTERIA-32**, own encoding and spec |
| Toolchain | clang/LLD | **Own** assembler, linker, C compiler |
| CPU timing | Passive, trace-driven analyzer; does not affect execution | **Three real cycle-level CPUs**; timing changes OS behaviour |
| Memory timing | Formula models; walks uncosted | Cycle-level caches, walker via L2, **DRAM bank state machines in their own clock domain** |
| Policies | `switch` statement; OPT gated for FIFO only | Pluggable interfaces; every policy gated against OPT |
| OS extras | – | Threads, sync primitives, deadlock detection, signals, permissions, disk scheduling |
| Observability | CLI + GDB | **Studio**: every cycle visible, reverse step |
