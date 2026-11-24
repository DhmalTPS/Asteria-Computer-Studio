#include "isa/csrs.hpp"

#include "common/ascii.hpp"

namespace asteria::isa {

std::optional<CsrInfo> find_csr(u16 number) noexcept {
    for (const CsrInfo& info : kCsrs) {
        if (static_cast<u16>(info.csr) == number) {
            return info;
        }
    }
    return std::nullopt;
}

std::optional<CsrInfo> find_csr(std::string_view name) noexcept {
    for (const CsrInfo& info : kCsrs) {
        if (ascii::iequals(info.name, name)) {
            return info;
        }
    }
    return std::nullopt;
}

}  // namespace asteria::isa
