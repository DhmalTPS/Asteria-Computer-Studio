# M0 Study Note: Foundation

**What M0 built:** the build system, the module skeleton, the architecture checker, the `common`
module (bit manipulation, simulated time, error handling, assertions), the test framework, CI, and a
CLI entry point.

**Read in this order** (about 30 minutes):
1. `CMakeLists.txt`, then `cmake/AsteriaModule.cmake`: how every module is declared
2. `cmake/CheckLayering.cmake`: how the dependency rules are enforced
3. `src/common/bits.hpp` and `tests/unit/common/bits_test.cpp`: **the most important file in M0**
4. `src/common/sim_time.hpp`, `src/common/result.hpp`, `src/common/assert.hpp`

---

## 1. Why build system choices matter for a simulator

**CMake presets** (`CMakePresets.json`) mean the whole configuration (generator, toolchain, build type)
is in version control. `cmake --preset debug` gives the same build on any machine and in CI. No
"works on my machine".

**vcpkg in manifest mode with a `builtin-baseline`**: the baseline is a vcpkg git commit, so every
library version is pinned. Builds are reproducible a year from now.

**Ninja** instead of Visual Studio solutions: faster, the same on every platform, and it builds exactly
what changed.

**One static library per module** (`asteria_add_module`). Each module is a separate compilation unit
with an explicit dependency list. A test executable links only the module it tests, so a cache test
cannot accidentally depend on the CPU.

## 2. Enforcing architecture with a test

All modules share the include root `src/`, so C++ alone cannot stop `cpu/` from writing
`#include "mem/cache/cache.hpp"`, and a header-only include would even link. CMake target dependencies
are therefore not enough. `CheckLayering.cmake` reads every `#include` line and checks it against the
allowed-dependency table. It runs as a normal CTest test, so **an architecture violation fails CI just
like a wrong answer does.**

This was verified both ways: an allowed include passes, and a forbidden one (`cpu` → `mem`) fails with
the file and line.

*Interview angle:* "How do you keep a large codebase's architecture from rotting?" Make the rules
executable. Documentation alone decays.

## 3. Bit manipulation (`bits.hpp`), the ISA's vocabulary

Every instruction decode in M1 is built from these functions:
```
ADDI R1, R0, 10  =  0x000A0044
opcode = extract(w, 5, 0)    = 0x04
rd     = extract(w, 10, 6)   = 1
rs1    = extract(w, 15, 11)  = 0
imm    = sign_extend(extract(w, 31, 16), 16) = 10
```

**`mask(32)` has a special case** because `1u << 32` is *undefined behaviour* in C++. Shifting by the
type's full width or more is UB, and on x86 the hardware actually masks the shift amount to 5 bits, so
`1u << 32` often evaluates to `1`. Bugs like this are the reason the test suite checks every
(hi, lo) pair.

**`sign_extend` without branches:** with `m` = the field's sign bit, `(v ^ m) - m`.
- If the sign bit is 0: XOR *adds* m, then we subtract it back, so the result is v.
- If the sign bit is 1: XOR *removes* m (v − m), then subtracting m again gives v − 2m = v − 2^width.
  That is exactly the two's-complement value, and the borrow sets every higher bit.

**`constexpr` + `static_assert`:** because these functions are `constexpr`, the key facts
(e.g. `extract(0x000A0044, 5, 0) == 0x04`) are checked **by the compiler**. A wrong implementation is a
build error, not just a failed test.

## 4. Simulated time (`sim_time.hpp`)

- **Femtoseconds**, because DDR4-2400 has an 833.33 ps clock. In femtoseconds that is 833'333, with
  negligible error. A u64 lasts about 5 hours of simulated time.
- **A strong type**, because the worst simulator bug class is mixing *time* with *cycles*. A 50 ns
  DRAM access is 63 cycles at 800 ps but 228 cycles at 220 ps (D8). `SimTime` cannot be added to an
  integer, so the conversion must go through `cycles_to_cover`, which **rounds up**: work that needs
  50.0001 ns isn't finished until the next clock edge.

## 5. Three kinds of failure

| Kind | Example | Mechanism |
|---|---|---|
| Bad input | `ADDI R1, R0, 99999` (immediate too large) | `Result<T>` (`std::expected`) |
| Asteria bug | cache index out of range | `ASTERIA_ASSERT`, also in Release |
| Machine behaviour | page fault, illegal instruction | **not an error at all**: a trap is data |

Why assertions stay on in Release: a simulator that continues after an invariant breaks produces
*plausible wrong numbers*, which is worse than crashing.

Why `std::expected` instead of exceptions: the failure is part of the function's signature, so it
can't be ignored silently, and control flow is visible at the call site.

---

## Quiz

Answer from memory, then check against the code.

1. Why can't CMake's `target_link_libraries` alone enforce the module layering in this repo? What closes the gap?
2. What does `1u << 32` evaluate to in C++, and what does x86 hardware typically produce? How does `mask()` avoid the problem?
3. Compute `sign_extend(0x0000'FFFE, 16)` by hand using the XOR/subtract trick. Show both steps.
4. Why does `insert()` mask `field` after shifting it? Give a value of `field` that would corrupt neighbouring bits without that mask.
5. Why femtoseconds rather than picoseconds? Name the clock that forces it.
6. `cycles_to_cover(SimTime::ns(50), SimTime::ps(220))` returns 228, not 227. Why round up? What would go wrong in the pipeline if it rounded down?
7. A page fault happens during simulation. Which of the three failure mechanisms handles it, and why is it *not* an error?
8. Why keep `ASTERIA_ASSERT` enabled in Release builds when `<cassert>` is disabled there?
9. What does the `builtin-baseline` in `vcpkg.json` guarantee, and why does that matter for a project you'll show in interviews a year from now?
10. The ISA puts the immediate's sign bit at bit 31 in every format. Using `bits.hpp`, explain why this makes the hardware immediate generator simpler.
