#pragma once
// ASTERIA_ASSERT(condition, message): checks an internal invariant.
//
// Two kinds of failure exist in Asteria, and they are handled differently:
//
//   1. Bad INPUT (malformed assembly, invalid cache config, corrupt executable).
//      Expected, reportable to the user -> return Result<T> (see error.hpp).
//
//   2. A BUG in Asteria itself (pipeline register in an impossible state, cache set index
//      out of range). Never expected -> ASTERIA_ASSERT, which stops immediately with the
//      file and line, so the bug cannot silently corrupt the simulation.
//
// Unlike <cassert>, this stays enabled in Release builds: a simulator that keeps running
// after an invariant breaks produces wrong results that look plausible, which is worse
// than a crash.

#include <cstdio>
#include <cstdlib>
#include <source_location>

namespace asteria::detail {

[[noreturn]] inline void assert_fail(
    const char* expression, const char* message,
    std::source_location where = std::source_location::current()) {
    std::fprintf(stderr, "%s:%u: assertion failed: %s (%s)\n", where.file_name(),
                 static_cast<unsigned>(where.line()), expression, message);
    std::abort();
}

}  // namespace asteria::detail

#define ASTERIA_ASSERT(condition, message) \
    ((condition) ? static_cast<void>(0) : ::asteria::detail::assert_fail(#condition, (message)))
