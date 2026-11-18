# Reference review: VirtualConsole

**Repo:** github.com/cwpobs/VirtualConsole · reviewed at commit `3f48abc` (2026-11-18) · read only, never built or run.
**Why it's on the list:** closest match to Asteria's *philosophy*: an original CPU and ISA, a memory map,
an assembler, a C-like compiler and a resident OS.
**Relevant to:** M1 (assembler), M2 (Studio), M8 (devices), M10 (compiler), M11 (OS).

## Verdict
A charming, working fantasy console. It is valuable as a **map of the whole territory** (ISA → assembler →
compiler → shell), but it is **not an architecture model**: there is no timing, no traps, no protection,
and its "OS" is mostly host C++ services. Learn from its toolchain *shape*. Do the opposite for its
system design.

## What it really is
- C++20, Windows-only (Win32 window, waveOut audio), Visual Studio project. About 13k lines of C++ excluding
  `stb_image.h`. Guest software: a 4.3k-line assembly shell plus about 15 Mini-C/asm programs. One flat
  directory with no tests, no CI and no layering. Comments are mostly in Russian.
- **Verified in code:** original ISA and interpreter, two-pass assembler recording relocations, Mini-C
  compiler (lexer → AST → assembly text), shell (`cd`/`dir`/`type`/`copy`/`build`/`exec`), nested exec up to
  depth 5, text/bitmap video, a 3D device, a MOD player.
- **Overstated:**
  - The assembler has no expressions, `.equ`, strings or macros, and its errors carry no line numbers.
  - `build`/`exec` are commands to the **disk device**, which runs the compiler and assembler **on the host**
    (`Disk.cpp:619-776`). Even the shell is assembled on the host at boot.
  - The 3D, physics and PNG devices are host services exposed through MMIO, not modelled hardware.

## Design in brief
| Area | How it works |
|---|---|
| ISA | 8-bit accumulator machine: 4 × 8-bit registers, 32-bit PC/SP/HL, Z/C flags. Variable-length byte encoding, about 35 opcodes, **no `MOV reg,reg`** |
| CPU | One giant `switch(opcode)` in `CPU::step()` (`CPU.cpp:110-944`). Timing = one cycle per bus access. **Unknown opcode silently stops the CPU** (`CPU.cpp:938`) |
| Interrupts | One flag. Pushes PC + FLAGS and jumps to fixed vector `0x5`. No cause register, no priorities |
| Bus | Linear scan over devices **on every byte** (`Bus.cpp:15-27`). Unmapped reads return 0 and writes vanish, so no bus errors. MMIO byte-packed with holes |
| Assembler | Line-splitting two-pass. Pass 1 (sizing) and pass 2 (encoding) are **two separate hand-written mnemonic lists**. No duplicate-label check |
| Compiler | Recursive descent with one function per precedence level, a declaration pre-pass, constant folding. Codegen leaves results in A with push/pop temporaries. **All locals are static globals**: no stack frames, no recursion. 8-bit `int` |
| OS | Shell patches fixed vectors, loads programs to a fixed sandbox address, and runs them with `CALL`. No processes, isolation or scheduling |

## Adopt in Asteria
| Idea | Where | Milestone |
|---|---|---|
| Assembler records which operands came from symbols, so even M1's flat image carries relocations, a cheap step toward `.aso` | `Assembler.h:16-30` | M1 |
| `assemble(source) → {image, relocations, diagnostics}` as a pure library function (CLI, Studio and tests all call it) | `Assembler.h` | M1 |
| Build errors as **data** for the editor. Asteria: `Diagnostic{file, line, column, message}` | `Disk.h:129-138` | M1/M2 |
| Rendering on its own thread, throttled, so presentation never starves simulation | `VideoConsole.cpp:349`, `main.cpp:277-300` | M2 (matches D19) |
| Separate "tickable" list from "address-mapped" list | `Bus.h:9-21` | M8 |
| Timer driven by simulated cycles, not wall time | `Timer.cpp:57-66` | M8 |
| Parser shape: precedence-level functions, declaration pre-pass, `foldConst` | `Compiler.h:98-111`, `Compiler.cpp:983, 2183` | M10 |
| First codegen strategy: result in one register, temporaries on the stack. Register allocation later | `Compiler.cpp:1987-2042` | M10 |
| Emit runtime helpers only if used | `Compiler.cpp:2205-2221` | M10 |
| **Generate** device constants (C header + asm `.equ`) from one memory-map table instead of hand-copying | `Compiler.cpp:34-111` | M8/M10 |

## Avoid (and Asteria's answer)
| Pitfall in VirtualConsole | Asteria's answer |
|---|---|
| Sizing and encoding tables duplicated across two passes, so they can drift | Encode, decode, size and disassemble all from the single `isa/` table (M1) |
| Errors without file and line. Line numbers restart at 1 inside `#include`d files | Every diagnostic carries file:line:column, and includes keep their origin |
| Toolchain and loader live inside a device, so the OS teaches nothing | Devices are dumb hardware. Loading, `exec` and FS are AsteriaOS code on the simulated CPU (D15, D22) |
| Unknown opcodes halt silently. Unmapped memory reads 0 | Precise `IllegalInstruction` and `BusError` traps (spec §5) |
| Host reads guest RAM to drive the UI. Guest reads PC through a "debug port" | Studio observes through snapshots only (D19). Guests use CSRs and `AUIPC` |
| Keyboard polled on wall-clock throttle, so nondeterministic | Inputs injected at recorded cycles (D14) |
| No stack frames in the compiler | M10 follows the ABI (spec §12) from day one |
| Fixed per-depth load addresses and relocation instead of address spaces | Per-process virtual address spaces (M7, M11) |
| No tests, docs drifting from code (shell address 0x3000 vs 0x20000) | Demo + tests + study note per milestone (ROADMAP) |
| No `MOV`, so codegen is full of `PUSH`/`POP` | Confirms the register-register base ISA plus `MOV`/`LI` pseudo-ops |
