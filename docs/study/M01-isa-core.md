# M1 Study Note: ISA Core

M1 turns the ASTERIA-32 specification into working code. It is delivered in four parts, and this note
grows one section per part:

| Part | What it builds | Status |
|---|---|---|
| 1 | Instruction table, encoder, decoder, disassembler (`src/isa/`) | done |
| 2 | Reference interpreter: the CPU that defines "correct" (`src/cpu/`, `src/mem/phys/`) | done |
| 3 | Assembler, the `.ast` executable format, the machine and the CLI (`src/toolchain/`, `src/machine/`) | done |
| 4 | ISA conformance suite and the CLI contract tests (`tests/isa/`, `tests/cli/`) | done |

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
          interpreter (M1)     single-cycle (M3)     multi-cycle (M4)
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
10. Why are `TLBFLUSH` and `FENCE.I` no-ops in the interpreter, and why won't they be in later CPUs (an instruction cache from M4–M5, TLBs in M7)?

---

## Part 3: the assembler, the executable format and the machine (`src/toolchain/`, `src/machine/`)

**Read in this order** (about 90 minutes):
1. `src/toolchain/as/assembler.cpp`: `run()`, then `pass1_line()`, `layout()`, `pass2()`, `finish()`.
   **The most important file in part 3.**
2. `src/toolchain/as/lexer.cpp`: one line of text into tokens
3. `src/toolchain/axf/axf.hpp` and `axf.cpp`, with [formats/AXF.md](../formats/AXF.md) open beside them
4. `src/machine/machine.cpp`: load an executable, run it, decide why it stopped
5. `apps/asteria/main.cpp`: the `build`, `run` and `disasm` commands
6. `src/toolchain/as/listing.cpp`: executable back to source
7. `tests/unit/toolchain/as/assembler_test.cpp`: every rule of spec §11 as a test

Try it:
```
asteria run examples/sum.asm --trace
asteria build examples/sum.asm
asteria disasm examples/sum.ast -o sum.lst.asm
asteria build sum.lst.asm -o again.ast        (again.ast is byte-identical to sum.ast)
```

### 1. The whole path, source to registers

```
 sum.asm ──lexer──► tokens ──pass 1──► statements + sizes + symbol table
                                   │
                              layout: give each section an address
                                   │
                    pass 2: encode every statement into bytes
                                   │
                    finish: segments + entry + symbols ──► AXF executable (.ast)
                                                                 │
                            Machine::load: copy segments into RAM, PC = entry, sp = top of RAM
                                                                 │
                            Machine::run: step the interpreter until HALT (or a reason to stop)
```

Each arrow is a separate function with its own tests. Nothing in `toolchain/` knows the simulator
exists (OVERVIEW §2). The `machine` is the only place where the two meet.

### 2. Why two passes

```asm
        BEQ  a0, zero, done      ; line 1: where is "done"? Not known yet.
        ...
done:   HALT
```

To encode line 1, the assembler needs the address of `done`, which appears later. So:

- **Pass 1** reads every line and works out **how many bytes** each statement takes. A location counter
  per section advances by that size, and each label gets (section, offset). Nothing is encoded yet.
- **Layout** places the sections: `.text` at `0x8000_0000`, then `.rodata`, `.data`, `.bss`, each on a
  fresh page. Now every label has an address.
- **Pass 2** reads the statements again and encodes them. Every symbol is known by now.

The catch: pass 1 must know every statement's size *before* it knows the labels. `LA` is always 2
instructions, so it can take a label. `LI` picks 1 or 2 instructions **depending on the value**, so the
value must be known in pass 1: a number, or a constant defined *above*. The same rule holds for `.space`,
`.align` and `.equ`. That is the "constant defined above" error. It isn't an arbitrary restriction: it
follows from having two passes.

Pass 2 runs even when pass 1 found errors, so one build reports everything. That is safe because a
statement that failed in pass 1 was never recorded, so pass 2 can't disagree about sizes. The errors
are then sorted back into source order.

### 3. Pseudo-instructions and splitting a 32-bit value

The hardware has 16-bit immediates. A 32-bit constant takes two instructions:

```
LI t0, 0x1234_8765     →   LUI t0, 0x1234          t0 = 0x1234_0000
                           ORI t0, t0, 0x8765      t0 = 0x1234_8765   (ORI zero-extends)
```

With `ADDI` instead of `ORI` there's a trap. `ADDI` **sign-extends**, so `0x8765` becomes `0xFFFF_8765`,
which is −0x789B, and the result comes out as `0x1233_8765`: one too small in the top half. `%hi_adj`
fixes this by rounding the top half up when bit 15 is set:

```
%hi_adj(0x1234_8765) = (0x1234_8765 + 0x8000) >> 16 = 0x1235
LUI t1, 0x1235  +  ADDI t1, t1, %lo(...)   =   0x1235_0000 − 0x789B   =   0x1234_8765  ✓
```

MIPS and RISC-V use the same trick (RISC-V's `%hi` is already the adjusted one). Asteria keeps both
operators, so `LUI` + `ORI` stays the simple case.

### 4. Branch targets are counted in instructions

`BEQ a0, a1, done` stores `(address(done) − PC) / 4` in the immediate. Counting instructions instead of
bytes gives 4× the reach from the same 16 bits, because the low two bits of a byte offset would always
be 0. The assembler rejects a target that isn't a multiple of 4 away, or is out of reach. In source,
`+2` and `-1` mean "that many instructions", and anything else is an address.

### 5. The executable format: one canonical form

An executable needs three things:
- what to load where: **segments**, each with R/W/X permissions;
- where to start: the **entry** point;
- names for addresses: **symbols**, for the disassembler and the debugger.

`.bss` costs nothing in the file: its segment has `mem_size` > 0 but no data, and the loader fills it
with zeros.

The design rule that keeps AXF trustworthy: a given executable has **exactly one** valid byte layout.
`write()` produces it, `read()` accepts nothing else, and `validate()` states every semantic rule once.
So `read(write(x)) == x` and `write(read(b)) == b`. A fuzz test flips random bits in a valid file 20,000
times, and checks that every file `read()` still accepts obeys both equations. Readers that accept
"almost right" files are a common source of security holes in real formats.

Segments start on page boundaries because, once the OS exists (M12), each page gets one set of
permissions in its PTE. `.text` is R+X and `.data` is R+W, so they can't share a page.

### 6. The machine: load, run, and why it stopped

`Machine::load` does five things, in order:
1. Validates the executable.
2. Checks that the segments fit in RAM.
3. Resets the CPU.
4. Writes every segment's **full** image, zeros included, so a second program can't see the first one's
   leftovers.
5. Sets PC = entry and `sp` = top of RAM.

`Machine::run` steps the interpreter and stops for exactly one of four reasons:

| Reason | When | CLI exit code |
|---|---|---|
| Halted | `HALT`; the exit code is `a0` | 0 if `a0` = 0, else 1 |
| StepLimit | `--max-steps` reached (default 10 million) | 3 |
| StuckTrap | the same trap at the same PC twice in a row: a trap that leads straight back to itself | 3 |
| WaitingForever | `WFI` with nothing to wake it (no devices until M8) | 3 |

StuckTrap matters because `TVEC` is 0 after reset. Take a program with no handler that faults:
1. The fault jumps to ROM address 0.
2. Address 0 holds the zero word, which is an illegal instruction.
3. That traps to 0 again, forever.

The CLI reports the **first** trap (the real bug), not only the loop it caused.

### 7. The listing proves the assembler and disassembler agree

`asteria disasm program.ast -o out.asm` writes a listing that **reassembles to the same bytes**, with
the labels and the entry point too. That is part 1's round trip (decode ∘ encode) lifted to whole
programs. It only works because the disassembler prints exactly the syntax the assembler reads
(part 1, §6). Words that aren't instructions are listed as `.word`, so even data in `.text` survives.

### Exam connection

- **Two-pass assembler** (Mano, ch. 6 "Assembler"): a *location counter LC* and an *address symbol
  table*. The first pass builds the table, and the second pass translates. That is `pass1()` and
  `pass2()`, with one location counter per section.
- **Assembler directives** (Hamacher, ch. 2: `ORIGIN`, `RESERVE`, `DATAWORD`, `EQU`) correspond to
  Asteria's section switches, `.space`, `.word` and `.equ`. Directives produce no instructions; they
  control the assembler.
- **Translating and starting a program** (P&H §2.12):
  - An object file has a header, text, data, relocation information, a symbol table and debug
    information.
  - The loader reads the header, copies text and data into memory, sets up the stack and jumps to the
    start.
  - That is AXF plus `Machine::load`. Relocation arrives with the linker in M9.
- **PC-relative addressing** (P&H §2.10): branch target = PC + offset × 4. Know why the offset counts
  words.
- **Pseudo-instructions** (P&H §2.12): `LI`, `MOV` and `BGT` don't exist in hardware; the assembler
  expands them. MIPS reserves `$at` for this. Asteria's expansions never need a scratch register.

### Quiz (part 3)

1. Why can't a one-pass assembler handle `BEQ a0, zero, done` when `done` is defined later? Name one
   way real one-pass assemblers get around it.
2. `LI a0, N` fails with "not a constant defined above" when `.equ N` comes later, but `ADDI a0, a0, N`
   works. Explain exactly why.
3. Write the two instructions for `LA t0, 0x8000_9ABC`. Then write the `LUI` + `ADDI` pair that loads
   the same value.
4. What does `.bss` cost in the `.ast` file, and what does it cost in memory?
5. Why does each section start on a new page?
6. `read(write(x)) == x` and `write(read(b)) == b`: why is the second equation the stronger guarantee?
7. Why does `Machine::load` write a segment's zeros explicitly, instead of only its data?
8. A program does `LW` from address 2 with no trap handler. Trace what happens, step by step, until the
   machine stops. What does the CLI print?
9. A branch at `0x8000_0010` targets `0x8000_0004`. What immediate does the assembler store?
10. How does the listing round trip (assemble → disassemble → assemble) catch a bug that the part 1
    round trip (encode → decode) can't?

---

## Part 4: the conformance suite (`tests/isa/`, `tests/cli/`)

**Read in this order** (about 60 minutes):
1. `tests/isa/README.md`: what each program covers, and what is deliberately left out
2. `tests/isa/alu.asm`: the simplest program; learn the check pattern here
3. `tests/isa/traps.asm`: a trap handler that logs every trap, and a trip into User mode
4. `tests/isa/paging.asm`: building page tables in assembly, then faulting on purpose
5. `tests/isa/illegal.asm`: strict decoding, tested by writing instructions at run time
6. `tests/isa/mutation_test.cpp`: how the suite tests *itself*
7. `tests/cli/CMakeLists.txt` and `cmake/ExpectExit.cmake`: the CLI's exit codes as a contract

Try it: `ctest --preset debug -R "conformance|roundtrip|cli"`

### 1. Why programs, not more unit tests?

The unit tests call C++ functions of *this* interpreter. M3–M4 build three new CPUs with different
insides, and C++ unit tests of the interpreter say nothing about them. A program in assembly is the
one thing every CPU accepts. So the specification is written down a second time, as executable
checks, in the only language all the CPUs share. The same ten files will run on the single-cycle,
multi-cycle and pipelined CPUs unchanged.

### 2. The check pattern

```asm
        LI      a0, 7                   ; check 7: what it tests
        <compute a result into t0>
        LI      t1, <the value the spec requires>
        BNE     t0, t1, fail            ; mismatch: halt with a0 = 7
        ...
        LI      a0, 0                   ; everything passed
fail:   HALT                            ; exit code = a0
```

There is no test framework: one register, one branch and `HALT`. That is deliberate. A program
that needs a working framework can't test the most basic things, and a broken CPU can't run a
framework. The expected values come from the **spec**, worked out by hand (the comments show the
arithmetic), never copied from the interpreter's output. Otherwise the suite would only prove that
the interpreter agrees with itself.

### 3. Testing traps: a logging handler

To check a trap you have to survive it. `traps.asm`, `paging.asm` and `illegal.asm` install a handler that:
1. Writes CAUSE, EPC, BADVADDR and STATUS (at entry) to a log in memory.
2. Returns past the faulting instruction (EPC + 4).
3. Handles two special cases:
   - a failed *fetch* returns to `ra`, because "the next instruction" after a bad jump target is
     also bad;
   - `ECALL` from User mode jumps back into the kernel instead of returning to the user.

The checks then compare log entries with what the spec says. User mode is entered the way a real OS
does it: clear `PIE` and `PKM`, put the user PC in `EPC`, and `ERET`.

**A subtle trap in testing traps.** "The last logged CAUSE is 12" proves nothing if the trap *before*
also logged 12: had the instruction under test wrongly succeeded, the old entry would still be there.
So every check that expects a trap also compares the logged **EPC**, which names the one instruction
that trapped. Two early checks (a `SWAP` fault right after a `SW` fault) had exactly this weakness.

### 4. Who tests the tests? Three questions

A check can be wrong in three different ways, and each needs a different tool.

| A check that… | looks like | is found by |
|---|---|---|
| **never runs** (a jump skips it) | pass | **Mutation 1, invert:** `BNE` → `BEQ`. The branch is now taken, so the program must fail. If it still passes, the check never executed. |
| **can never fail** (`BNE t0, t0, fail`) | pass | **Mutation 2, corrupt:** insert `XORI t0, t0, 1` just before it. A real comparison now fails. A tautology still passes. |
| **expects the wrong value** | pass | **No mutation can.** If the interpreter and the test share a misreading of the spec, they agree. |

Why both mutations? Inverting a tautology `BNE t0, t0` gives `BEQ t0, t0`, which always jumps to
`fail`, so the mutant dies and the useless check looks fine. Inversion only proves a check *runs*.
Corruption proves it *depends on the result*. `mutation_test.cpp` applies both to every check in every
program. It was itself tested by planting a dead check and a tautology: both were
reported at once.

**That planting found a bug in the mutation test itself.** The first version "killed" the planted
tautology, which looked right and was wrong. The corrupted mutants were built from a `std::smatch`
whose text had already been overwritten (a dangling reference). None of them even assembled, and "does
not build" counted as "failed", so operator 2 had passed every check vacuously. Two fixes:
- copy the match groups to strings before mutating;
- a mutant now has to **build and then fail**. "Does not build" is reported as an error, never
  counted as a kill.

With operator 2 really working, it found one genuine weak check: `BNE s1, s1, fail` in
`branch.asm`. A CPU that compared register *numbers* instead of values would pass it. It now
compares two different registers that hold the same value.

The lesson: a check that has never been seen to fail is not yet a check, and that includes the
checks on the checks.

The third row is the **oracle problem**: who checks the expected values? Two defences:
1. Every value is worked out from the spec by hand (the comments show the arithmetic), never copied
   from the interpreter.
2. An **independent re-derivation**: a reviewer saw only the spec and the test programs, never the
   implementation, and recomputed all 92 checks. Result: **0 disagreements**, 4 places where the spec
   was ambiguous (now settled in the spec text), and a list of rules no check touched yet (now added;
   see `tests/isa/README.md`). An ambiguity is worth as much as a bug: the M3 and M4 CPUs would have
   had to guess.

### 5. Gaps found by asking "what isn't covered?"

Reviewing the suite against the spec, section by section, turned up rules no test touched yet, so
they were added:
- **Alignment is checked before translation** (§4.4): a misaligned load to an *unmapped* page must
  report `LoadMisaligned`, not a page fault.
- **A bus error during the page walk** (§8.4) reports the access's BusError with the *virtual*
  address.
- **Code written at run time** (§9): store an instruction, `FENCE.I`, execute it. In M1 there is no
  I-cache, so this passes trivially; in M4 it is the test that catches a missing cache flush.
- **The CLI's exit codes** (0, 1, 2, 3): a script relies on them, so `tests/cli/` checks each one,
  together with its message. This found a real inconsistency: `asteria` with no command exited 0.
  Now it exits 2, like any other usage error.
- **Strict decoding as a program** (`illegal.asm`): the unit tests check the decoder, but a CPU in M3
  has its own decoder. `illegal.asm` stores each of 53 illegal words into a slot, runs `FENCE.I`, and
  calls it. Every one must trap as `IllegalInstruction` at that slot. Check 1 first runs a *legal*
  stored word, which proves the slot really executes what was stored. Without it, every "must trap"
  check could pass vacuously.
- **One list of programs.** The mutation test reads the program list from the same CMake variable
  that creates the CTest entries, so no program can join the suite without being mutation-tested.

What is still not covered, and why (interrupts, selective `TLBFLUSH`), is listed in
`tests/isa/README.md`. Saying what is *not* tested is part of a test suite.

### Exam connection

- **Verification vs. validation**: the suite verifies that an implementation matches the spec. A
  CPU textbook's "test program" for a datapath (Patterson & Hennessy ch. 4 exercises) is the same
  idea on a small scale.
- **Exceptions in a pipeline** (P&H §4.9, Hamacher): the trap tests check *precise* behaviour (rd not
  written, memory unchanged, EPC = the faulting instruction). These are exactly the properties a
  pipeline can get wrong when a later stage has already started.
- **Self-modifying code and caches** (P&H ch. 5, split I/D caches): why an instruction cache can
  hold a stale copy, and why `FENCE.I` exists.

### Quiz (part 4)

1. Why can't the interpreter's unit tests check the M3–M4 CPUs?
2. Why must expected values come from the spec, not from running the interpreter?
3. In `traps.asm`, why does a failed fetch return to `ra` instead of `EPC + 4`?
4. How does the kernel enter User mode for the first time? Which STATUS bits does it set up, and
   why does it use `ERET` rather than a jump?
5. What is a mutant? Why does inverting `BNE t0, t0, fail` *not* reveal that the check is useless,
   and which mutation does?
6. `LW t0, 2(t3)` with `t3` pointing to an unmapped page: which exception, and why that one?
7. Why does the `FENCE.I` check pass trivially today, and when will it start to matter?
8. Why does a CLI's exit code deserve tests of its own?
9. What is the oracle problem, and why can no mutation solve it?
10. Why does every trap check compare EPC and not just CAUSE?

---

## M1 final quiz (all four parts)

Answer without notes, then check against the sections above.

1. Trace `BEQ a0, a1, done` from source text to a taken branch: the assembler's two passes, the
   encoding, decode, and `PC + imm × 4`.
2. Why is decoding strict, and which later tool depends on it?
3. Name the rule that makes traps precise, and show it for `SW` to a read-only page with paging on.
4. What do trap entry and `ERET` do to STATUS? Why is it "a stack of depth one"?
5. Walk VA `0x0040_2010` through the tables of `paging.asm`: which entries are read, what is the
   physical address, and which PTE bits change?
6. Why does `LI` need a constant defined above, while `LA` accepts any label?
7. `%hi_adj`: when does it differ from `%hi`, and why?
8. What does an `.ast` file contain, and why is there exactly one valid encoding of each executable?
9. Name the four ways `asteria run` can stop, with their exit codes.
10. How do you know the conformance suite would catch a broken CPU?
