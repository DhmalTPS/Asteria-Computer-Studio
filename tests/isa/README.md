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
| `traps.asm` | every synchronous exception with paging off; ROM, MMIO, the end of RAM; read-only CSRs; trap entry/`ERET`; what User mode may and may not do (4.4, 4.7, 4.8, 5, 6, memory map) |
| `paging.asm` | the page walk, superpages (including a non-identity one), permissions, execute-only pages, A/D bits, fault subcodes, User pages, the TLB rule (8) |
| `illegal.asm` | strict decoding: all 27 unassigned opcodes, undefined `funct11` values, non-zero unused fields, CSR numbers that don't exist (2, 3, 4.3, 4.7, 4.8) |

## The convention

- `a0` holds the number of the check that is running. A check compares a result with the value the
  spec requires and branches to `fail` on a mismatch.
- `fail: HALT`. The program halts with `a0` = the failed check, so `asteria run` exits with code 1
  and prints which check failed. When every check passes, the program sets `a0 = 0` and falls into
  the same `HALT`: exit code 0.
- A program that never halts (a stuck trap loop or a runaway loop) exits with code 3.
- `traps.asm`, `paging.asm` and `illegal.asm` install a handler that logs every trap (CAUSE, EPC, …)
  to memory, so the checks can compare the exact trap sequence. A check that expects a trap also
  compares the logged **EPC**: the code alone could be left over from the previous trap, and an
  instruction that wrongly did *not* trap would go unnoticed.

## Not covered here, and why

- **Interrupts.** In M1 nothing can raise an interrupt line from a program (devices arrive in M8),
  so the interrupt and `WFI` rules are covered by the interpreter's unit tests. They join this
  suite with the timer in M8.
- **`TLBFLUSH` with an address or an ASID.** Without a TLB (M1) every flush is a no-op, so only the
  rule "remap, flush, then the new mapping is used" is checked. The selective forms get tests when
  the TLB exists (M7).
- **Exact `INSTRET` around a trap.** "A trapping instruction doesn't count" is checked in the unit
  tests, where the handler's own instructions don't blur the count.
- **Interrupt timing and `CYCLE` values.** These may differ between CPUs by design (spec §10);
  only `CYCLE ≥ instructions` is checked.

## Who checks the checks

Three different questions, answered three different ways:

| Question | Answered by |
|---|---|
| Does every check **run**? | Mutation 1: invert it (`BNE` ↔ `BEQ`, `BLT` ↔ `BGE`, …). A passing check's branch is not taken, so the inverted one is, and the program must fail. If it still passes, the check never executed. |
| Can every check **fail**? | Mutation 2: flip bit 0 of the register an equality check tests (`XORI r, r, 1` before `BNE r, …` / `BNEZ r`). If the program still passes, the check doesn't depend on the result, for example `BNE t0, t0, fail`. Inversion can't find that one: inverted, a tautology always fails. |
| Is every expected value **right**? | Not by any mutation. If the interpreter and a test share a misreading of the spec, both agree and pass (the *oracle problem*). Each expected value was derived from the spec by hand, and the whole suite was then **re-derived independently**, from the spec alone without reading the implementation (below). |

`mutation_test.cpp` (CTest `conformance_mutations/...`) runs both mutations on every program listed
in `ISA_PROGRAMS`, in CI, so a check added later is held to the same rules. A mutant must **build
and then fail**. One that doesn't assemble proves nothing and is reported as an error. (An early
version counted those as kills, which silently disabled mutation 2; see the M1 study note, part 4 §4.)

### The independent re-derivation (M1)

A reviewer given only `docs/isa/ASTERIA-32.md`, `docs/platform/MEMORY-MAP.md` and these programs, and
not `src/` or the unit tests, recomputed all 92 checks of that time by hand. Results:

- **Disagreements: none.**
- **Ambiguities: 4**, each now settled in the spec:
  - `CYCLE` must advance at least once per instruction (§7);
  - `IPEND`'s reset value is "live", 0 with no devices (§7);
  - "rs1 = R0" means the rs1 *field*, so `CSRRS t0, CPUID, t1` is illegal even when t1 = 0 (§4.8);
  - the W-without-R subcode is 1 (§8.4).
- **Untested rules**, all since added as checks:
  - MMIO, ROM and end-of-RAM accesses;
  - loads into R0 that fault;
  - writes to every kind of read-only CSR;
  - User-mode counters, `TLBFLUSH`, `EBREAK` and `FENCE.I`;
  - software-set PIE/PKM honoured by `ERET`;
  - execute-only pages;
  - A already set with D clear;
  - A set by a fetch;
  - a non-identity superpage;
  - kernel access to U pages;
  - faulting stores leaving memory unchanged;
  - User fetches from U = 0 pages, and User stores to pages without W;
  - `illegal.asm`;
  - a backward `JAL`;
  - `JALR` misaligned by 1 and 3;
  - a negative `AUIPC`;
  - negative dividends divided by zero;
  - mixed-sign `MULH`.

  Two weak checks (a `SWAP` fault whose logged code matched the previous trap's) now compare EPC too.

## Running

`ctest --preset debug -R conformance/` runs the suite, or run one program directly:

```
asteria run tests/isa/paging.asm
```

Each program is also a round-trip test (`roundtrip/<name>`): assemble, disassemble, reassemble, and
compare the two executables byte for byte (`cmake/RoundTrip.cmake`).
