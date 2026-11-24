# M1 Study Note: ISA Core

M1 turns the ASTERIA-32 specification into working code. It is delivered in four parts, and this note
grows one section per part:

| Part | What it builds | Status |
|---|---|---|
| 1 | Instruction table, encoder, decoder, disassembler (`src/isa/`) | done |
| 2 | Reference interpreter: the CPU that defines "correct" | next |
| 3 | Assembler and the `.ast` executable format | |
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
