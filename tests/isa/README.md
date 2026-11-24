# ISA conformance suite

Self-checking assembly programs, one per instruction group. They define what every Asteria CPU must
do: the reference interpreter passes them now, and the single-cycle, multi-cycle and pipelined CPUs
(M2–M4) must pass the same files unchanged.

| Program | Covers (spec section) |
|---|---|
| `alu.asm` | register-register ALU, shifts by `rs2[4:0]`, signed vs unsigned compare, R0 (4.1) |
| `alu_imm.asm` | immediate ALU, sign- vs zero-extension, `LUI`, `AUIPC`, `%hi`/`%lo`/`%hi_adj` (4.3) |
| `muldiv.asm` | `MUL*`, `DIV*`, `REM*`, division by zero, `INT_MIN / -1` (4.2) |
| `memory.asm` | every load and store width, sign extension, little-endian order, `SWAP` (4.4, 4.4a) |
| `branch.asm` | every branch taken and not taken, signed vs unsigned, loops, pseudo-branches (4.5) |
| `jump.asm` | `JAL`/`JALR` links, offsets, `rd = rs1`, recursion through the stack (4.6) |
| `csr.asm` | CSR read/set/clear forms, reset values, writable masks, `INSTRET`, `CYCLE` (4.8, 7) |
| `traps.asm` | every synchronous exception with paging off, trap entry/`ERET`, User mode (4.7, 5, 6) |
| `paging.asm` | the page walk, superpages, permissions, A/D bits, fault subcodes, User pages, TLB rule (8) |

## The convention

- `a0` holds the number of the check that is running. A check compares a result with the value the
  spec requires and branches to `fail` on a mismatch.
- `fail: HALT`. The program halts with `a0` = the failed check, so `asteria run` exits with code 1
  and prints which check failed. When every check passes, the program sets `a0 = 0` and falls into
  the same `HALT`: exit code 0.
- A program that never halts (a stuck trap loop or a runaway loop) exits with code 3.
- `traps.asm` and `paging.asm` install a handler that logs every trap (CAUSE, EPC, BADVADDR, STATUS
  at entry) to memory, so the checks can compare the exact trap sequence.

## Not covered here, and why

- **Interrupts.** In M1 nothing can raise an interrupt line from a program (devices arrive in M8),
  so the interrupt and `WFI` rules are covered by the interpreter's unit tests. They join this
  suite with the timer in M8.
- **`TLBFLUSH` with an address or an ASID.** Without a TLB (M1) every flush is a no-op, so only the
  rule "remap, flush, then the new mapping is used" is checked. The selective forms get tests when
  the TLB exists (M3).
- **Exact `INSTRET` around a trap.** "A trapping instruction doesn't count" is checked in the unit
  tests, where the handler's own instructions don't blur the count.
- **Interrupt timing and `CYCLE` values.** These may differ between CPUs by design (spec §10);
  only `CYCLE ≥ instructions` is checked.

## Every check can fail

`mutation_test.cpp` (CTest `conformance_mutations/...`) inverts each `B… , fail` line in turn
(`BNE` ↔ `BEQ`, `BLT` ↔ `BGE`, …) and requires every such mutant program to fail. A mutant that
still passes means a check that never runs, or can never fail. It runs in CI with everything else,
so a check added later is held to the same rule.

## Running

`ctest --preset debug -R conformance/` runs the suite, or run one program directly:

```
asteria run tests/isa/paging.asm
```

Each program is also a round-trip test (`roundtrip/<name>`): assemble, disassemble, reassemble, and
compare the two executables byte for byte (`cmake/RoundTrip.cmake`).
