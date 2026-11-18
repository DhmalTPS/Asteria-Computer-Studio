#pragma once
// Result<T>: the return type of every operation that can fail because of its INPUT.
//
//     Result<Program> assemble(std::string_view source);
//
//     auto program = assemble(text);
//     if (!program) { show(program.error().message); return; }
//     run(*program);
//
// Why std::expected instead of exceptions? The failure is part of the function's type, so a
// caller cannot forget it exists, and the cost is visible at every call site. Exceptions are
// reserved for what a program cannot sensibly handle (out of memory). Internal bugs use
// ASTERIA_ASSERT (assert.hpp).

#include <expected>
#include <format>
#include <string>
#include <utility>

namespace asteria {

struct Error {
    std::string message;
};

template <class T>
using Result = std::expected<T, Error>;

// Builds a failed Result with a std::format message:
//     return fail("line {}: unknown mnemonic '{}'", line, name);
template <class... Args>
[[nodiscard]] std::unexpected<Error> fail(std::format_string<Args...> format, Args&&... args) {
    return std::unexpected<Error>{Error{std::format(format, std::forward<Args>(args)...)}};
}

}  // namespace asteria
