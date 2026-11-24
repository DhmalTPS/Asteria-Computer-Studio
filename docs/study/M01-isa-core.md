# M1 Study Note: ISA Core

M1 turns the ASTERIA-32 specification into working code. It is delivered in four parts, and this note
grows one section per part:

| Part | What it builds | Status |
|---|---|---|
| 1 | Instruction table, encoder, decoder, disassembler (`src/isa/`) | done |
| 2 | Reference interpreter: the CPU that defines "correct" (`src/cpu/`, `src/mem/phys/`) | done |
| 3 | Assembler and the `.ast` executable format | next |
| 4 | ISA conformance suite | |

---

## Part 1: the instruction set as data (`src/isa/`)

**Read in this order** (about 45 minutes):
1. `src/isa/instruction_set.hpp`: the table of all 59 instructions. **The most important file in part 1.**
2. `src/isa/encoding.hpp`: `decode()`, and the spec's examples checked at compile time
3. `src/isa/encoding.cpp`: `encode()`
4. `src/isa/disassemble.cpp`, `registers.hpp`, `csrs.hpp`, `traps.hpp`
5. `tests/unit/isa/encoding_test.cpp`: how the round trip is proven

Try it: `asteria disasm 000A0044 0002_08C1 FFE00FA1 00000000`

### 1. One table, everything else derived (D23)

Every instruction is one row: mnemonic, opcode, funct11, **form**, kernel-only flag.

```
{Op::Add,  "ADD",  0x01, 0, Form::RegRegReg, false},
{Op::Addi, "ADDI", 0x04, 0, Form::RegRegImm, false},
```

The **form** says which fields an instruction uses and how its immediate is read. Three small functions
derive everything else from it: `format_of(form)` (R/I/S/B/J), `fields_used(form)` (which of rd/rs1/rs2),
and `imm_kind_of(form)` (Signed16, Unsigned16, Shamt5, Csr12, SplitSigned16, Signed21).

*Why:* one of the reviewed simulators (LC-John) decoded instructions in three separate places, and the three
copies disagreed. Here a fact about an instruction exists **once**. The decoder, encoder, disassembler and
(in part 3) the assembler all read the same row.

### 2. The compiler checks the table

`detail::table_is_consistent()` runs at **compile time** (`consteval` + `static_assert`). It checks that:
- row order matches the `Op` enum, so `info(op)` is a plain array index;
- no opcode is 0 (D3: the zero word must stay illegal);
- only R-format rows have a funct value;
- opcodes are shared only inside an R-format group, with distinct functs;
- mnemonics are unique.

A wrong table **does not build**. The spec's encoding examples (§13) are checked the same way, at the bottom
of `encoding.hpp`.

### 3. How decoding works

```
word ──► opcode = word[5:0] ──► kByOpcode[opcode] ──► {first row, count}
              count = 0 → reserved opcode → illegal
              R-format group → compare word[31:21] with each row's funct
         ──► check unused fields are zero ──► build the immediate ──► Instruction
```

`kByOpcode` is a 64-entry table **built from the instruction table at compile time**, not typed by hand.

The immediate rules, straight from spec §2:

| Kind | Bits | Example |
|---|---|---|
| Signed16 | `sext(word[31:16])` | ADDI, loads, JALR |
| Unsigned16 | `zext(word[31:16])` | ANDI/ORI/XORI, LUI, AUIPC |
| SplitSigned16 | `sext(word[31:21] : word[10:6])` | stores, branches |
| Signed21 | `sext(word[31:11])` | JAL |

### 4. Strict decoding makes encode and decode exact inverses

The spec says unused fields **must be zero** (D3). So for every legal word:
```
encode(decode(w)) == w        and        decode(encode(i)) == i
```

If the decoder ignored unused bits instead, 32 different words could all decode to `ECALL` (any value in the
rd field). Then disassembling and reassembling a program would not give back the same bytes. It would also
make the differential fuzzer (M3) blind: two CPUs could disagree about a junk bit and still "match".

The test `encode inverts decode for every legal 32-bit word` checks two million random words.

### 5. Two kinds of "wrong", handled differently

| Situation | Mechanism | Why |
|---|---|---|
| `decode()` meets an illegal word | returns `nullopt` | Not an error. The CPU turns it into an `IllegalInstruction` **trap** (machine behaviour, part 2). |
| `encode()` is given `ADDI` with immediate 40000 | returns a `Result` error with a message | Bad input. The assembler shows it to the user (part 3). |
| `encode()` produces a word that doesn't decode back | `ASTERIA_ASSERT` | Would be a bug in Asteria itself. |

These are the three failure kinds from M0, now used for real.

### 6. The disassembler prints what the assembler reads

`BEQ R1, R2, +3` means "3 instructions forward". The target is printed **relative**, with an explicit sign,
because a relative offset is all the word contains: the disassembler may not know where the word sits in
memory. Part 3's assembler reads a leading `+`/`-` as exactly this, so the output can always be assembled
again. Unsigned immediates print in hex (`ORI R1, R1, 0xFFFF`), signed ones in decimal (`ADDI R1, R0, -2`).

### 7. CSR descriptions (`csrs.hpp`)

Each CSR has a number, name, access rights, reset value and a **writable mask**. Two spec gaps were closed
while writing this file:
- Undefined CSR bits read as 0 and ignore writes.
- `TVEC` has only modes 0 and 1, so bit 1 reads as 0.

Both are now written in spec §5.4 and §7.

### Exam connection

- **Instruction formats and field-width questions:** "32 registers → 5-bit register fields; 6-bit opcode → 64
  opcodes; what is left for the immediate?" This is exactly the R/I/S/B/J layout.
- **Why fixed fields help hardware:** registers can be read before decoding finishes. The same argument
  is made for MIPS and RISC-V in Patterson & Hennessy, chapter 2.
- **Opcode + function field** (MIPS `funct`, here `funct11`): one opcode for a whole group of ALU operations.

### Quiz (part 1)

1. Why is `kByOpcode` built at compile time from the table, instead of being written by hand?
2. Decode `0xFFE00FA1` by hand: opcode, format, rs1, rs2, and the immediate (show how the two pieces join).
3. Why does `decode()` return `nullopt` for `ECALL` with a non-zero rd field, when the CPU could just ignore it?
4. `encode({Op::Sw, 5, 1, 2, 0})` fails. Why? What does "rd" mean for a store?
5. Which instructions sign-extend their 16-bit immediate, and which zero-extend it? Why does ORI zero-extend?
6. Why does the disassembler print `BEQ R1, R2, +3` and not a target address?
7. What does the compile-time check do if two rows get the same opcode and funct?
8. What do `encode()` and `decode()` each do with a value that is out of range, and why do they differ?

---

## Part 2: the reference interpreter (`src/cpu/`, `src/mem/phys/`)

**Read in this order** (about 75 minutes):
1. `src/cpu/core/semantics.cpp`: what every instruction does. **The most important file in part 2.**
2. `src/cpu/core/exec_context.hpp`: the interface the semantics run against
3. `src/cpu/core/csr_file.cpp`: trap entry and ERET
4. `src/cpu/interp/interpreter.cpp`: one `step()`: interrupt check, fetch, decode, execute
5. `src/cpu/interp/page_walk.cpp`: the two-level walk, line by line against spec §8.4
6. `src/mem/phys/physical_bus.cpp` and `src/sim/functional_memory.hpp`
7. `tests/unit/cpu/interp/interpreter_test.cpp`: each test is a small program with its expected state

### 1. Why a "reference" interpreter?

Asteria will have three CPUs (single-cycle, multi-cycle, pipeline). How do you know the pipeline is right?
Compare it with something simpler that is obviously right. The interpreter is that: no timing, no pipeline,
no TLB, just the spec executed one instruction at a time. Spec §10 defines "correct" as "ends in the same
state as the interpreter". So the interpreter is written for **clarity**, never speed.

### 2. Semantics written once (D23)

```
                 semantics.cpp: execute(instruction, pc, ExecContext&)
                        │  "what ADD does"
                        ▼
   ExecContext:  regs()  csrs()  load()  store()  swap()  fence_i()  tlb_flush()
                        ▲ implemented by
          interpreter (M1)     single-cycle (M2)     multi-cycle (M3)
          "how": functional    "how": a datapath     "how": FSM states
```

`execute()` never knows which CPU it runs in, so the three CPUs **cannot** disagree about what an
instruction means. They can only differ in timing, and timing is what Asteria compares.

The ALU (`alu.hpp`) knows 18 operations, not 27 instructions: ADDI is ADD with the immediate as the
second operand. `alu_op_of()` is that mapping. The pipeline's control word will carry the same `AluOp`.

### 3. Precise traps: check everything, then change anything (D2)

Spec §5.1 says a trapping instruction has **no effect**. In code that becomes an ordering rule: every
check that can fail runs before the first write.

| Instruction | Checks first | Then writes |
|---|---|---|
| any K-only | privilege | – |
| `LW` | alignment → translation → bus | rd |
| `SW` | alignment → translation → bus → PTE A/D | memory |
| `JALR` | target alignment | rd, PC |
| `CSRRW` | CSR exists, writable, privilege | CSR, rd |

The memory side has a subtle case. Setting a page-table **Accessed** bit is itself a memory write. If the
interpreter set A and *then* found that the data access fails, a trapping instruction would have changed
memory. So the order inside `Interpreter::prepare()` is:

```
translate (reads only) ──► bus.accepts(data access)? ──► write PTE A/D back ──► do the access
```

That is why `FunctionalMemory` has `accepts()`: "would this succeed?" without doing it.

### 4. One step

```
step():
  stalled WFI?        → complete it once a line is pending, else stay Waiting
  IE and pending?     → take the interrupt (EPC = this PC, nothing executed)
  fetch               → translate, bus check; may raise InstrPageFault / InstrBusError
  decode              → nullopt means IllegalInstruction
  execute             → Next / Trap / Halt / Wait
  CYCLE += 1          (INSTRET += 1 only if the instruction completed)
```

The order of the checks *is* the exception priority of spec §5.2: fetch faults (1, 2), then decode (3),
then privilege (4), then the instruction's own exceptions. No extra code is needed to get priority right.

### 5. Trap entry and ERET: a stack of depth one

```
trap:  PIE ← IE,  PKM ← KM,  IE ← 0,  KM ← 1      ("push" the old mode)
ERET:  IE ← PIE,  KM ← PKM                         ("pop" it back)
```

A user program that makes an ECALL arrives in the kernel with interrupts off. ERET returns to User mode
with interrupts as they were. A trap inside a handler overwrites PIE/PKM and EPC, which is why a kernel
saves them first (spec §5.1).

### 6. Paging: the walk as a pure function

`walk()` only **reads**. It returns either a fault or `{physical address, which PTE needs A/D}`. The caller
decides when to write. The fault subcodes:

| Situation | CAUSE[15:8] |
|---|---|
| V = 0 at either level, or a pointer at level 0 | 0 not present |
| R/W/X/U check failed, or W = 1 with R = 0 | 1 permission |
| level-1 leaf whose PPN[9:0] ≠ 0 | 2 misaligned superpage |

Without a TLB there is nothing to flush, so `TLBFLUSH` and `FENCE.I` are no-ops here. They start to matter
in M3/M4, when there is a TLB and an I-cache that can go stale.

### 7. Spec gaps closed while writing part 2

- **HALT:** PC stays at the HALT, and it counts as completed.
- **ERET:** PIE and PKM keep their values.
- **WFI:** when woken it *completes*, and an enabled interrupt is taken at the next boundary, so EPC points
  after the WFI. Otherwise the handler's ERET would land back in the WFI and wait again.
- **Paging:** page tables must be in ROM/RAM, and the A/D write-back is the last check.

All are written into the spec (§4.7, §8.4), the memory map and D7.

### Exam connection

- **Precise exceptions** (Hamacher, pipelining chapter; Patterson & Hennessy §4.9): "all earlier instructions
  have completed; the faulting one and all later ones have no effect". The check-order table above is how a
  simple design guarantees it.
- **Interrupt handling sequence** (Mano: "save PC, disable interrupts, branch to the service routine"):
  exactly `enter_trap()`. Vectored vs direct mode is Mano's vectored interrupt vs polling one handler.
- **Two-level page tables** (P&H §5.7, Stallings): VPN1 indexes the root, VPN0 the second level, and a
  superpage is a leaf at the first level. The dirty bit tells the OS which pages must be written to disk
  before their frame is reused.
- **Division by zero:** some ISAs trap (x86), some return a defined value (RISC-V, Asteria). Know both.

### Quiz (part 2)

1. Why does Asteria need a reference interpreter if the CPUs will also execute every instruction?
2. What would break if the single-cycle CPU had its own copy of the instruction semantics?
3. A `SW` to a read-only page: list every piece of state that must *not* change, and where the code
   guarantees it.
4. Why does `FunctionalMemory` need `accepts()`? Describe the failure without it.
5. Which steps of `step()` produce exception codes 1, 2, 3 and 4, and why does that order equal the priority?
6. After ECALL from User mode with IE = 1, what are IE, KM, PIE, PKM? And after the handler's ERET?
7. Why must a stalled WFI *complete* before its wake-up interrupt is taken?
8. `0x8000_0000 / -1` is undefined behaviour in C++. What does Asteria return, and how does the code avoid
   the undefined behaviour?
9. VA `0x0040_0123` maps through a 4 KiB leaf with PPN `0x80020`. What is the physical address? What if the
   leaf were a superpage with PPN `0x80000`?
10. Why are `TLBFLUSH` and `FENCE.I` no-ops in the interpreter, and why won't they be in M3/M4?
