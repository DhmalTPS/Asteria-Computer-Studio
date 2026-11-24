# AXF: the Asteria eXecutable Format

**Status:** version 1.0, the executable (`.ast`) subset, implemented in M1
(`src/toolchain/axf/`). Relocatable objects (`.aso`), relocations and the line table come with the
linker in M9 (D17).

AXF is a small ELF-like container. An executable holds three things:

- **segments**: what to load, where, and with which permissions (the loader needs this),
- an **entry point**: where execution starts,
- a **symbol table**: names for addresses (the disassembler and the debugger use it).

All integers are little-endian. Offsets are from the start of the file.

## 1. File layout

```
+--------------------------+  0
| header (32 bytes)        |
+--------------------------+  32
| segment table            |  segment_count × 20 bytes
+--------------------------+
| symbol table             |  symbol_count × 12 bytes
+--------------------------+
| string table             |  strtab_size bytes
+--------------------------+
| segment data             |  each segment's initialized bytes, in segment order, packed
+--------------------------+  end of file
```

### Header (32 bytes)
| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 4 | magic | `"AXF\0"` (`41 58 46 00`) |
| 4 | 2 | version major | 1 |
| 6 | 2 | version minor | 0 |
| 8 | 2 | type | 2 = EXEC (`.ast`). 1 = REL (`.aso`) is reserved for M9 |
| 10 | 2 | reserved | 0 |
| 12 | 4 | entry | virtual address of the first instruction |
| 16 | 4 | segment_count | |
| 20 | 4 | symbol_count | |
| 24 | 4 | strtab_size | bytes in the string table |
| 28 | 4 | reserved | 0 |

### Segment entry (20 bytes)
| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | vaddr | load address, page-aligned |
| 4 | 4 | file_offset | where its initialized bytes are in the file |
| 8 | 4 | file_size | how many initialized bytes |
| 12 | 4 | mem_size | bytes in memory; those past `file_size` are zero (this is how `.bss` costs no file space) |
| 16 | 4 | flags | bit 0 R, bit 1 W, bit 2 X |

### Symbol entry (12 bytes)
| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | name_offset | offset of the name in the string table |
| 4 | 4 | value | the address |
| 8 | 4 | flags | bit 0: global (declared with `.global`). Other bits are 0 |

The **string table** holds each symbol's name, NUL-terminated, in symbol order.

## 2. One canonical form

A given executable has exactly one valid byte layout. The writer produces it, and the reader accepts
nothing else:

- the tables follow each other with no gaps, in the order above;
- the string table holds exactly one name per symbol, in symbol order, with no unused bytes;
- segment data follows the string table in segment order, packed; the file ends right after it;
- reserved fields and unused flag bits are 0.

So `read(write(x)) == x`, and `write(read(b)) == b` for every file `b` that reads. A fuzz test flips
random bits of a valid file and checks that every file the reader still accepts obeys both rules.

## 3. Semantic rules

`validate()` states these once. The writer refuses an executable that breaks one, and the reader
checks every file, so no tool ever sees a malformed executable.

1. There is at least one segment.
2. Each segment: `vaddr` is page-aligned (4096); `mem_size > 0`; `file_size ≤ mem_size`; it does not
   run past 2³²; its flags have R set and use only R, W and X.
3. Segments are sorted by address, and no two touch the same page.
4. The entry point is 4-aligned and lies inside the initialized data of an X segment.
5. Symbol names are non-empty, contain no NUL, and are unique.
6. Symbols are sorted by (value, name).

Why page granularity (rule 3): when the OS loads a program (M12), each page gets one set of
permissions in the page table. Two segments that share a page could not have different permissions.

## 4. Loading (M1)

The `asteria` machine loads an executable into RAM at its virtual addresses (paging is off at reset,
so they are physical addresses too):

1. Check every segment fits in RAM (`0x8000_0000` up to the RAM size).
2. Reset the CPU.
3. Write each segment's full image: its data, then zeros up to `mem_size`.
4. Set PC = entry and `sp` = the top of RAM (the stack grows down).

Segment permissions are recorded but not enforced in M1: there is no page table yet. The OS loader
(M12) maps each segment with its flags.

## 5. What M9 adds

Type REL (`.aso`) files with sections, relocations, undefined (`.extern`) symbols and a line table.
Version 1.0 readers refuse type 1 with "linking arrives in M9".
