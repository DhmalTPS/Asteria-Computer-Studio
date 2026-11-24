#include "isa/registers.hpp"

#include "common/ascii.hpp"

namespace asteria::isa {

std::optional<u8> parse_register(std::string_view name) noexcept {
    for (unsigned n = 0; n < kRegisterCount; ++n) {
        // Exact canonical spellings only, so "R05" is rejected rather than silently read as R5.
        if (ascii::iequals(name, kNumericNames[n]) || ascii::iequals(name, kAbiNames[n])) {
            return static_cast<u8>(n);
        }
    }
    if (ascii::iequals(name, "s8")) {
        return reg::kFp;  // spec §12: fp doubles as saved register s8
    }
    return std::nullopt;
}

}  // namespace asteria::isa
