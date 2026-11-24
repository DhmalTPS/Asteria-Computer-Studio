# Coding Style

The goal is code a reader can study: every file says what it is for, every non-obvious line
says *why*, and the same idea always looks the same.

## Naming
| Thing | Style | Example |
|---|---|---|
| Types, classes, enums | `PascalCase` | `SimTime`, `CacheConfig` |
| Functions, variables | `snake_case` | `sign_extend`, `miss_count` |
| Private data members | `snake_case_` (trailing underscore) | `fs_`, `lines_` |
| Constants | `kPascalCase` | `kVersion`, `kPageSize` |
| Macros (rare) | `ASTERIA_UPPER_CASE` | `ASTERIA_ASSERT` |
| Namespaces | mirror the folder | `asteria::mem::cache` |
| Files | `snake_case.hpp` / `.cpp`, tests `<name>_test.cpp` | `sim_time.hpp` |

## Files
- Every header begins with a comment block: **what** it provides, **why** it exists, and the spec
  section or decision (`D9`) it implements.
- `#pragma once`. Include order: standard/third-party `<...>`, then Asteria `"module/..."`, in two groups.
- Include what you use. Never rely on a header pulling in another one transitively.

## Types
- Architectural values are `u32`. Reinterpret as signed only where the operation is signed (`types.hpp`).
- Strong types for quantities that must not mix: `SimTime` vs cycle counts, and later physical vs
  virtual addresses.
- `enum class` only, never plain enums.

## Errors
| Failure | Mechanism |
|---|---|
| Bad input (user assembly, config, file) | return `Result<T>`, built with `fail("...")` |
| Internal invariant broken (a bug) | `ASTERIA_ASSERT(cond, "why")`, enabled in every build |
| Simulated machine fault (page fault, illegal instruction) | **not an error**: it is architectural behaviour, modelled as data (a trap record) |

The third row matters: a page fault is the machine working correctly, so it never uses C++ error machinery.

## Simulation code
- Stateful components follow **evaluate → commit** (D10). `evaluate()` must not change visible state.
- No heap allocation in per-cycle paths. Allocate at construction.
- No hidden globals. Everything a component uses is passed in or owned.
- Determinism (D14): no clocks, no unseeded randomness, no iteration over unordered containers where
  the order affects results.

## Comments
- Explain *why*, not *what*. `// x += 1` comments are deleted on sight.
- Reference the spec: `// spec §4.6: JALR reads rs1 before writing rd`.
- Use worked examples where the logic is subtle (see `sign_extend` in `common/bits.hpp`).

## Tests
- Every public function has tests in `tests/unit/<module>/`.
- Use `static_assert` for constexpr facts, and exhaustive loops for small domains (all 256 bytes,
  every bit range).
- Test names are sentences: `TEST_CASE("sign_extend widens two's-complement fields")`.
- Test names are **ASCII only** (write "spec section 4", not "§4"). CTest passes each name back to the test
  binary as a filter, and on Windows a non-ASCII character is re-encoded on the way, so the test is
  silently not found and reported as failed.
