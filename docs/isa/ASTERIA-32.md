# ASTERIA-32 Instruction Set Architecture

**Version 1.0-draft.** This document is normative: the reference interpreter (`src/cpu/interp`) and all
three CPUs must implement exactly what it says. If code and spec disagree, the code has a bug,
or the spec gets amended here first.

Notation: `x[hi:lo]` is a bit slice. `sext(v)` sign-extends v to 32 bits; `zext(v)` zero-extends it.
`R[n]` is register n. All arithmetic is modulo 2³². `PC` is the address of the *current* instruction.

---

## 1. Architectural state

| State | Width | Reset value | Notes |
|---|---|---|---|
| `R0`–`R31` | 32 | 0 | `R0` reads as 0. Writes to it are discarded |
| `PC` | 32 | `0x0000_0000` | Always a multiple of 4 when an instruction executes |
| CSRs | 32 each | see §7 | Control and status registers |
| Memory | 2³² bytes, physical | RAM zeroed (D14) | Little-endian, byte-addressable |

Reset state: Kernel mode, interrupts disabled, paging disabled.

## 2. Instruction formats

All instructions are 32 bits, little-endian in memory, and 4-byte aligned.

```
      31                21 20     16 15     11 10      6 5      0
  R  |      funct11       |   rs2   |   rs1   |   rd    | opcode |
  I  |          imm[15:0]           |   rs1   |   rd    | opcode |
  S/B|     imm[15:5]      |   rs2   |   rs1   |imm[4:0] | opcode |
  J  |                imm[20:0]               |   rd    | opcode |
```

Design invariants (D3):
- `rd` is always at [10:6], `rs1` at [15:11], `rs2` at [20:16]. The register file can be read before the format is known.
- The immediate's sign bit is always bit 31.
- `opcode = 0x00` is illegal in every form, so the all-zero word traps.
- Fields an instruction doesn't use must be zero, otherwise `IllegalInstruction`.

Immediate decoding:
```
I-imm  = sext(inst[31:16])                         16-bit signed
S/B-imm= sext(inst[31:21] : inst[10:6])            16-bit signed
J-imm  = sext(inst[31:11])                         21-bit signed
```

## 3. Opcode map

| Opcode | Mnemonic | Fmt | | Opcode | Mnemonic | Fmt |
|---|---|---|---|---|---|---|
| `0x00` | *illegal* | – | | `0x18` | SB | S |
| `0x01` | OP (ALU group) | R | | `0x19` | SH | S |
| `0x02` | MULDIV group | R | | `0x1A` | SW | S |
| `0x04` | ADDI | I | | `0x20` | BEQ | B |
| `0x05` | SLTI | I | | `0x21` | BNE | B |
| `0x06` | SLTIU | I | | `0x22` | BLT | B |
| `0x07` | ANDI | I | | `0x23` | BGE | B |
| `0x08` | ORI | I | | `0x24` | BLTU | B |
| `0x09` | XORI | I | | `0x25` | BGEU | B |
| `0x0A` | SLLI | I | | `0x28` | JAL | J |
| `0x0B` | SRLI | I | | `0x29` | JALR | I |
| `0x0C` | SRAI | I | | `0x30` | SYSTEM group | R |
| `0x0D` | LUI | I | | `0x31` | CSRRW | I |
| `0x0E` | AUIPC | I | | `0x32` | CSRRS | I |
| `0x10` | LB | I | | `0x33` | CSRRC | I |
| `0x11` | LH | I | | `0x34` | CSRRWI | I |
| `0x12` | LW | I | | `0x35` | CSRRSI | I |
| `0x14` | LBU | I | | `0x36` | CSRRCI | I |
| `0x15` | LHU | I | | `0x1C` | AMO group | R |

Every opcode not listed is reserved and raises `IllegalInstruction`.

### funct11 values
| Group | funct11 → mnemonic |
|---|---|
| OP `0x01` | `0`ADD `1`SUB `2`AND `3`OR `4`XOR `5`NOR `6`SLL `7`SRL `8`SRA `9`SLT `10`SLTU |
| MULDIV `0x02` | `0`MUL `1`MULH `2`MULHU `3`DIV `4`DIVU `5`REM `6`REMU |
| AMO `0x1C` | `0`SWAP |
| SYSTEM `0x30` | `0`ECALL `1`EBREAK `2`ERET `3`WFI `4`HALT `5`TLBFLUSH `6`FENCE.I |

## 4. Instruction semantics

### 4.1 Register-register ALU (OP, R-type)
| Instr | Operation |
|---|---|
| `ADD rd, rs1, rs2` | rd ← rs1 + rs2 |
| `SUB rd, rs1, rs2` | rd ← rs1 − rs2 |
| `AND / OR / XOR` | rd ← rs1 op rs2 |
| `NOR rd, rs1, rs2` | rd ← ~(rs1 \| rs2) |
| `SLL rd, rs1, rs2` | rd ← rs1 << rs2[4:0] |
| `SRL rd, rs1, rs2` | rd ← rs1 >>ᵤ rs2[4:0] (zero fill) |
| `SRA rd, rs1, rs2` | rd ← rs1 >>ₛ rs2[4:0] (sign fill) |
| `SLT rd, rs1, rs2` | rd ← (signed rs1 < signed rs2) ? 1 : 0 |
| `SLTU rd, rs1, rs2` | rd ← (unsigned rs1 < unsigned rs2) ? 1 : 0 |

No instruction raises an arithmetic exception (D4).

### 4.2 Multiply / divide (MULDIV, R-type)
| Instr | Operation |
|---|---|
| `MUL` | rd ← (rs1 × rs2)[31:0] |
| `MULH` | rd ← (signed rs1 × signed rs2)[63:32] |
| `MULHU` | rd ← (unsigned rs1 × unsigned rs2)[63:32] |
| `DIV` / `REM` | signed quotient / remainder, rounding toward zero |
| `DIVU` / `REMU` | unsigned quotient / remainder |

Special cases (no traps):
| Case | Quotient | Remainder |
|---|---|---|
| divisor = 0 | `0xFFFF_FFFF` | dividend |
| `DIV`: `0x8000_0000 / −1` | `0x8000_0000` | 0 |

Execution latency is microarchitectural: 1 cycle on single-cycle; configurable on multi-cycle and
pipeline (defaults MUL 3, DIV 32).

### 4.3 Immediate ALU (I-type)
| Instr | Operation | Immediate |
|---|---|---|
| `ADDI rd, rs1, imm` | rd ← rs1 + imm | **sext** |
| `SLTI` | rd ← (signed rs1 < signed imm) | **sext** |
| `SLTIU` | rd ← (unsigned rs1 < unsigned sext(imm)) | **sext**, then compared unsigned |
| `ANDI / ORI / XORI` | rd ← rs1 op imm | **zext** |
| `SLLI / SRLI / SRAI rd, rs1, sh` | shift by imm[4:0] | imm[15:5] must be 0 |
| `LUI rd, imm` | rd ← imm << 16 | rs1 field must be 0 |
| `AUIPC rd, imm` | rd ← PC + (imm << 16) | rs1 field must be 0 |

Logical immediates are zero-extended so that `LUI rd, hi` + `ORI rd, rd, lo` builds any 32-bit constant
without carry correction.

### 4.4 Loads and stores
Effective address `EA = rs1 + sext(imm)`.

| Instr | Operation | Alignment |
|---|---|---|
| `LB rd, imm(rs1)` | rd ← sext(M8[EA]) | any |
| `LBU` | rd ← zext(M8[EA]) | any |
| `LH` / `LHU` | rd ← sext / zext(M16[EA]) | EA % 2 = 0 |
| `LW` | rd ← M32[EA] | EA % 4 = 0 |
| `SB rs2, imm(rs1)` | M8[EA] ← rs2[7:0] | any |
| `SH` | M16[EA] ← rs2[15:0] | EA % 2 = 0 |
| `SW` | M32[EA] ← rs2 | EA % 4 = 0 |

- A misaligned access raises `Load/StoreMisaligned` with `BADVADDR = EA`. Nothing is accessed (D5).
- The alignment check happens **before** translation.
- A load into `R0` still performs the access, including any MMIO side effect and fault. The result is discarded.

### 4.4a Atomic memory operation (AMO group, R-type, D21)
| Instr | Operation |
|---|---|
| `SWAP rd, rs2, (rs1)` | **Indivisibly:** t ← M32[rs1]; M32[rs1] ← rs2; rd ← t |

- The address is `rs1` with no offset. It must be 4-byte aligned, otherwise `StoreMisaligned`.
- `SWAP` counts as a **store** for faults: it needs the page to have both R and W, and failures raise
  `StorePageFault` / `StoreBusError`. The walker sets both A and D.
- `SWAP` on MMIO raises `StoreBusError`.
- Nothing can come between the read and the write: no interrupt, no DMA access.
- `rd`, `rs1` and `rs2` may name the same register. All sources are read before `rd` is written.
- Typical use, a test-and-set spinlock: `LI t0, 1` → `loop: SWAP t1, t0, (a0)` → `BNEZ t1, loop`.

### 4.5 Branches (B-type)
Target `T = PC + sext(imm) × 4`, relative to **the branch's own address**. Range ±128 KiB.

| Instr | Taken if |
|---|---|
| `BEQ rs1, rs2, label` | rs1 = rs2 |
| `BNE` | rs1 ≠ rs2 |
| `BLT` / `BGE` | signed rs1 < rs2 / signed rs1 ≥ rs2 |
| `BLTU` / `BGEU` | unsigned rs1 < rs2 / unsigned rs1 ≥ rs2 |

If taken PC ← T, otherwise PC ← PC + 4. There are no delay slots. T is always 4-byte aligned by construction.

### 4.6 Jumps
| Instr | Operation |
|---|---|
| `JAL rd, label` (J) | rd ← PC + 4; PC ← PC + sext(imm21) × 4. Range ±4 MiB |
| `JALR rd, rs1, imm` (I) | t ← rs1 + sext(imm); if t % 4 ≠ 0 → `InstrMisaligned` (BADVADDR = t, rd not written); else rd ← PC + 4, PC ← t |

`JALR` reads `rs1` before writing `rd`, so `rd = rs1` is legal.

### 4.7 System instructions (SYSTEM group, R-type)
Unless stated otherwise, the `rd`, `rs1` and `rs2` fields must be zero.

| Instr | Priv | Operation |
|---|---|---|
| `ECALL` | any | Raise `EnvironmentCall`. EPC = PC (the handler must add 4 before `ERET`) |
| `EBREAK` | any | Raise `Breakpoint`. An attached debugger intercepts it first |
| `ERET` | K | IE ← PIE; KM ← PKM; PC ← EPC |
| `WFI` | K | Stall until `(IPEND & IMASK) ≠ 0`, regardless of IE, then continue at PC + 4 |
| `HALT` | K | Stop the machine. Exit code = `R4` (`a0`). Tests pass with `a0 = 0` |
| `TLBFLUSH rs1, rs2` | K | Invalidate cached translations (§8.6). `rs1`/`rs2` are used |
| `FENCE.I` | any | Make all earlier stores visible to instruction fetch (§9) |

Executing a K instruction in User mode raises `PrivilegeViolation`.

### 4.8 CSR access (I-type, `imm[15:0]` = CSR number; `imm[15:12]` must be 0)
| Instr | Operation (t = old CSR value) |
|---|---|
| `CSRRW rd, csr, rs1` | t ← csr; csr ← rs1; rd ← t. **If rd = R0 the CSR is not read** |
| `CSRRS rd, csr, rs1` | t ← csr; csr ← t \| rs1; rd ← t. **If rs1 = R0 the CSR is not written** |
| `CSRRC rd, csr, rs1` | t ← csr; csr ← t & ~rs1; rd ← t. Same rule |
| `CSRRWI/SI/CI rd, csr, uimm5` | As above, using `zext(rs1 field)` as the 5-bit operand |

An unknown CSR, or a write to a read-only CSR, raises `IllegalInstruction`. Accessing a K-only CSR
from User mode raises `PrivilegeViolation`.

## 5. Exceptions and interrupts

### 5.1 Trap entry (identical for exceptions and interrupts)
```
STATUS.PIE ← STATUS.IE ;  STATUS.PKM ← STATUS.KM
STATUS.IE  ← 0         ;  STATUS.KM  ← 1           (Kernel mode)
EPC        ← PC of the faulting instruction, or of the next unexecuted instruction for an interrupt
CAUSE      ← code (bit 31 = 1 for interrupts)
BADVADDR   ← faulting address (address exceptions only; otherwise unchanged)
PC         ← trap target (§5.4)
```
The faulting instruction and all later ones have **no architectural effect** (precise, D2).
A trap taken while `IE = 0` (an exception inside a handler) still happens and overwrites EPC.
The kernel must save `EPC`/`STATUS` before doing anything that can fault.

### 5.2 Exception causes (`CAUSE[31] = 0`)
If one instruction raises several exceptions, the lowest number in this table wins:

| Code | Name | BADVADDR |
|---|---|---|
| 0 | `InstrMisaligned` (bad `JALR` target) | target |
| 1 | `InstrPageFault` | PC |
| 2 | `InstrBusError` | PC |
| 3 | `IllegalInstruction` | – |
| 4 | `PrivilegeViolation` | – |
| 5 | `Breakpoint` | – |
| 6 | `EnvironmentCall` | – |
| 7 | `LoadMisaligned` | EA |
| 8 | `StoreMisaligned` | EA |
| 9 | `LoadPageFault` | EA |
| 10 | `StorePageFault` | EA |
| 11 | `LoadBusError` | EA |
| 12 | `StoreBusError` | EA |

`CAUSE[15:8]` carries a subcode for page faults: `0` = not present (V = 0 at some level),
`1` = permission denied, `2` = misaligned superpage. A **TLB miss is not an exception**: the hardware
walker resolves it (D6).

### 5.3 Interrupts (`CAUSE[31] = 1`, `CAUSE[2:0] = IRQ`)
- 8 interrupt lines. `IPEND[i]` reflects line i (read-only). `IMASK[i]` enables it.
- An interrupt is taken **at an instruction boundary** when `STATUS.IE = 1` and `(IPEND & IMASK) ≠ 0`.
  The lowest pending, enabled IRQ number wins.
- Lines are level-sensitive. The handler must clear the source at the device (or interrupt controller),
  otherwise the interrupt is taken again immediately after `ERET`.

| IRQ | Source |
|---|---|
| 0 | Timer |
| 1 | Disk |
| 2 | DMA |
| 3 | Keyboard |
| 4 | Network |
| 5 | Display |
| 6–7 | Reserved |

### 5.4 Trap target
`TVEC[1:0]` selects the mode. `TVEC[31:2] << 2` is the base.
- Mode 0 (direct): every trap jumps to base.
- Mode 1 (vectored): exceptions jump to base. IRQ *i* jumps to base + 4 × (1 + i).

## 6. Privilege
- `STATUS.KM = 1` means Kernel mode, 0 means User mode.
- User mode cannot execute `ERET`, `WFI`, `HALT` or `TLBFLUSH`, and cannot access CSRs except the read-only
  counters (`CYCLE`, `CYCLEH`, `INSTRET`, `INSTRETH`, `CPUID`).
- With paging on, User mode can access only pages with `U = 1`. Kernel mode can access all pages.
- With paging off, there is no memory protection in either mode.

## 7. Control and status registers

| Num | Name | Access | Reset | Contents |
|---|---|---|---|---|
| `0x000` | `STATUS` | K rw | `0x2` | [0] IE · [1] KM · [2] PIE · [3] PKM · others 0 |
| `0x001` | `CAUSE` | K rw | 0 | [31] interrupt · [15:8] subcode · [7:0] code/IRQ |
| `0x002` | `EPC` | K rw | 0 | Trap PC. Bits [1:0] read as 0 |
| `0x003` | `BADVADDR` | K rw | 0 | Faulting address |
| `0x004` | `TVEC` | K rw | 0 | [31:2] base · [1:0] mode |
| `0x005` | `SCRATCH` | K rw | 0 | Free for the kernel (holds the kernel stack pointer on trap entry) |
| `0x006` | `PTBR` | K rw | 0 | [31:12] root table PPN · [11:4] ASID · [0] EN (paging on) |
| `0x007` | `IMASK` | K rw | 0 | [7:0] per-IRQ enable |
| `0x008` | `IPEND` | K ro | – | [7:0] live interrupt lines |
| `0x010` | `CYCLE` | ro | 0 | Low 32 bits of the cycle counter |
| `0x011` | `CYCLEH` | ro | 0 | High 32 bits |
| `0x012` | `INSTRET` | ro | 0 | Low 32 bits of instructions retired |
| `0x013` | `INSTRETH` | ro | 0 | High 32 bits |
| `0x0F0` | `CPUID` | ro | `0x0001_0000` | [31:16] ISA major · [15:0] ISA minor |

`CYCLE` is the **only** architecturally visible value that may legitimately differ between the
three CPUs. The differential tester treats reads of it as non-comparable (§10).
`CPUID` deliberately does **not** identify the microarchitecture.

## 8. Virtual memory

### 8.1 Enabling paging
Paging is on when `PTBR.EN = 1`. Otherwise physical address = virtual address for every access.
Writing `PTBR` takes effect for the next instruction fetch (the pipeline flushes after any CSR write).

### 8.2 Address split (4 KiB pages)
```
VA = VPN1[31:22] | VPN0[21:12] | offset[11:0]
```

### 8.3 Page table entry (32 bits)
```
 31                 12 11   8  7   6   5   4   3   2   1   0
|        PPN          | SW  | D | A | G | U | X | W | R | V |
```
- `V`: valid. `R`/`W`/`X`: permissions. `U`: user-accessible. `G`: global (matches every ASID).
  `A`/`D`: accessed/dirty, set by hardware. `SW`: ignored by hardware, for OS use.
- `R = W = X = 0` with `V = 1`: pointer to the next-level table at `PPN << 12`.
- Any of R/W/X set: **leaf**. `W = 1` with `R = 0` is reserved and causes a page fault.

### 8.4 Walk algorithm (hardware, D6)
```
1. a ← PTBR.PPN << 12 ; level ← 1
2. pte ← M32[a + VPN_level × 4]        (physical read, via L2; bus error → access-type BusError)
3. if pte.V = 0 or (pte.W = 1 and pte.R = 0) → PageFault(subcode 0 / 1)
4. if pte is a pointer:
       if level = 0 → PageFault(subcode 0)
       a ← pte.PPN << 12 ; level ← 0 ; goto 2
5. leaf: if level = 1 and pte.PPN[9:0] ≠ 0 → PageFault(subcode 2)   (misaligned superpage)
6. permission check (§8.5) → PageFault(subcode 1) on failure
7. if pte.A = 0, or (store and pte.D = 0): set A (and D for a store) and write the PTE back
8. PA = level 1 → PPN[19:10] : VPN0 : offset       (4 MiB superpage)
        level 0 → PPN : offset                      (4 KiB page)
```

### 8.5 Permission check
| Access | Requires |
|---|---|
| Instruction fetch | X |
| Load | R |
| Store | W |
| Any access in User mode | additionally U = 1 |

### 8.6 TLB rules
The TLB is microarchitectural, but software must follow these rules:
- The hardware may cache any **valid** leaf PTE it has walked. **Invalid PTEs are never cached.** Making a
  PTE valid therefore needs no flush (demand paging is cheap). Making it invalid, or reducing its
  permissions, requires `TLBFLUSH`.
- `TLBFLUSH rs1, rs2`: if the rs1 field is `R0`, all addresses; otherwise only the page containing `R[rs1]`.
  If the rs2 field is `R0`, all ASIDs **including global entries**; otherwise only ASID `R[rs2][7:0]`, and
  global entries are kept.
- Writing `PTBR` (changing ASID) does **not** flush the TLB. Entries are tagged with their ASID.

## 9. Memory ordering
- One hart: loads and stores take effect in program order.
- MMIO accesses (physical `0xF000_0000`+) are uncached and never reordered, merged or speculated.
- **Instruction fetch is not coherent with stores.** After writing instructions to memory (for example the
  loader), software must execute `FENCE.I` before jumping to them. `FENCE.I` writes back dirty data-cache
  lines and invalidates the instruction cache (D13).
- DMA is coherent with the data caches in hardware (D13).
- `SWAP` is atomic with respect to interrupts and DMA (§4.4a).

## 10. Architectural equivalence (what "same result" means)
Two implementations are equivalent on a program if, at `HALT`, they agree on `R1`–`R31`, `PC`, all CSRs
except `CYCLE`/`CYCLEH`, all RAM contents, all device output, and the exact sequence of traps taken
(cause, EPC, BADVADDR).
- Programs that read `CYCLE`, or that rely on interrupt *timing*, are excluded from strict comparison.
- The differential tester injects interrupts at fixed **instruction counts**, not at fixed times, so
  interrupt behaviour is still comparable.

## 11. Assembly language

### 11.1 Syntax
```asm
; comment                        # also a comment
label:                           .L_local:   (file-local; not exported)
    ADDI  R1, R0, 10             ; registers: R0–R31 or ABI names (sp, ra, a0 …)
    LW    t0, 8(sp)              ; memory operand: imm(base)
    BEQ   a0, zero, done
```
Mnemonics and register names are case-insensitive. Labels are case-sensitive.

### 11.2 Pseudo-instructions
| Pseudo | Expands to |
|---|---|
| `NOP` | `ADDI R0, R0, 0` (= `0x0000_0004`, deliberately not 0) |
| `MOV rd, rs` | `ADDI rd, rs, 0` |
| `LI rd, imm32` | `ADDI` if it fits in 16 signed bits; `LUI` if the low half is 0; else `LUI` + `ORI` |
| `LA rd, sym` | `LUI rd, %hi(sym)` + `ORI rd, rd, %lo(sym)` |
| `NOT rd, rs` | `NOR rd, rs, R0` |
| `NEG rd, rs` | `SUB rd, R0, rs` |
| `SEQZ rd, rs` / `SNEZ rd, rs` | `SLTIU rd, rs, 1` / `SLTU rd, R0, rs` |
| `BEQZ / BNEZ rs, L` | `BEQ / BNE rs, R0, L` |
| `BGT / BLE / BGTU / BLEU a, b, L` | `BLT / BGE / BLTU / BGEU b, a, L` |
| `J L` | `JAL R0, L` |
| `JAL L` (one operand) / `CALL L` | `JAL R31, L` |
| `JR rs` | `JALR R0, rs, 0` |
| `RET` | `JALR R0, R31, 0` |
| `CSRR rd, csr` / `CSRW csr, rs` | `CSRRS rd, csr, R0` / `CSRRW R0, csr, rs` |

`%hi(sym)` is the upper 16 bits. `%lo(sym)` is the lower 16 bits, zero-extended for `ORI`. When `%lo` is
used in a sign-extending context (`LW rd, %lo(sym)(rt)`), the linker uses `%hi_adj` = `(sym + 0x8000) >> 16`.

### 11.3 Directives
`.text .data .rodata .bss` (sections) · `.global sym` · `.extern sym` · `.word .half .byte` ·
`.ascii .asciz` · `.space n` · `.align n` (2ⁿ) · `.equ name, value` · `.include "file"` ·
`.macro name args … .endm` · `.entry sym`

## 12. Calling convention (ABI)

| Reg | ABI name | Role | Saved by |
|---|---|---|---|
| R0 | `zero` | Constant 0 | – |
| R1 | `at` | Assembler temporary (pseudo-instruction expansion) | – |
| R2–R3 | `v0`–`v1` | Return values; `v0` = syscall number | caller |
| R4–R9 | `a0`–`a5` | Arguments 1–6 | caller |
| R10–R17 | `t0`–`t7` | Temporaries | caller |
| R18–R25 | `s0`–`s7` | Saved | **callee** |
| R26–R27 | `t8`–`t9` | Temporaries | caller |
| R28 | `gp` | Global pointer | – |
| R29 | `sp` | Stack pointer | **callee** |
| R30 | `fp` | Frame pointer (`s8`) | **callee** |
| R31 | `ra` | Return address | caller |

- The stack grows **down**. `sp` is 16-byte aligned at every call. There is no red zone.
- Arguments 7+ go on the stack at `0(sp)`, `4(sp)`, … at the moment of the call.
- A 64-bit return value goes in `v0` (low) and `v1` (high).
- **System calls:** number in `v0`, arguments in `a0`–`a5`, `ECALL`, result in `v0`. Negative values in
  `-4095..-1` are errors (`-errno`). All other registers are preserved.

## 13. Encoding examples
| Assembly | Fields | Machine word |
|---|---|---|
| `ADDI R1, R0, 10` | op 04, rd 1, rs1 0, imm 10 | `0x000A_0044` |
| `ADD R3, R1, R2` | op 01, rd 3, rs1 1, rs2 2, f 0 | `0x0002_08C1` |
| `BEQ R1, R2, +3` | op 20, rs1 1, rs2 2, imm 3 | `0x0002_08E0` |
| `NOP` | op 04, all 0 | `0x0000_0004` |
| *(zero word)* | op 00 | `IllegalInstruction` |
