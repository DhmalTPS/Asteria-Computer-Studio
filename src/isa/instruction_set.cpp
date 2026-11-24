#include "isa/instruction_set.hpp"

#include "common/ascii.hpp"

namespace asteria::isa {

std::optional<Op> find_op(std::string_view mnemonic) noexcept {
    for (const OpInfo& row : kInstructionSet) {
        if (ascii::iequals(row.mnemonic, mnemonic)) {
            return row.op;
        }
    }
    return std::nullopt;
}

}  // namespace asteria::isa
