#pragma once
// The 32 general-purpose registers (spec §1). R0 always reads as 0, and writes to it are
// discarded: enforced here, once, so no CPU model can forget it.

#include <array>

#include "common/assert.hpp"
#include "common/types.hpp"
#include "isa/registers.hpp"

namespace asteria::cpu::core {

class RegisterFile {
public:
    [[nodiscard]] u32 read(unsigned index) const noexcept {
        ASTERIA_ASSERT(index < isa::kRegisterCount, "register index out of range");
        return values_[index];
    }

    void write(unsigned index, u32 value) noexcept {
        ASTERIA_ASSERT(index < isa::kRegisterCount, "register index out of range");
        if (index != isa::reg::kZero) {
            values_[index] = value;
        }
    }

    void reset() noexcept { values_.fill(0); }

    friend bool operator==(const RegisterFile&, const RegisterFile&) = default;

private:
    std::array<u32, isa::kRegisterCount> values_{};  // values_[0] is never written
};

}  // namespace asteria::cpu::core
