#include <catch2/catch_test_macros.hpp>

#include "isa/csrs.hpp"
#include "isa/disassemble.hpp"
#include "isa/registers.hpp"

using namespace asteria;
using namespace asteria::isa;

namespace {
std::string dis(u32 word, RegisterNames names = RegisterNames::Numeric) {
    const auto in = decode(word);
    REQUIRE(in.has_value());
    return disassemble(*in, names);
}
}  // namespace

TEST_CASE("disassembly prints each form in its canonical syntax") {
    CHECK(dis(0x000A'0044) == "ADDI R1, R0, 10");
    CHECK(dis(0x0002'08C1) == "ADD R3, R1, R2");
    CHECK(dis(0x0002'08E0) == "BEQ R1, R2, +3");
    CHECK(dis(0xFFE0'0FA1) == "BNE R1, R0, -2");
    CHECK(dis(0x8001'00CD) == "LUI R3, 0x8001");
    CHECK(dis(0x0002'181A) == "SW R2, 0(R3)");
    CHECK(dis(0x0080'0030) == "HALT");
    CHECK(dis(kNop) == "ADDI R0, R0, 0");
    CHECK(dis(encode({Op::Lw, 5, 29, 0, -8}).value()) == "LW R5, -8(R29)");
    CHECK(dis(encode({Op::Ori, 1, 1, 0, 0xFFFF}).value()) == "ORI R1, R1, 0xFFFF");
    CHECK(dis(encode({Op::Srai, 7, 8, 0, 31}).value()) == "SRAI R7, R8, 31");
    CHECK(dis(encode({Op::Jal, 31, 0, 0, 0}).value()) == "JAL R31, +0");
    CHECK(dis(encode({Op::Jalr, 0, 31, 0, 0}).value()) == "JALR R0, R31, 0");
    CHECK(dis(encode({Op::Swap, 9, 4, 10, 0}).value()) == "SWAP R9, R10, (R4)");
    CHECK(dis(encode({Op::Tlbflush, 0, 4, 5, 0}).value()) == "TLBFLUSH R4, R5");
    CHECK(dis(encode({Op::FenceI}).value()) == "FENCE.I");
}

TEST_CASE("CSR operands print by name, unknown numbers as hex") {
    CHECK(dis(encode({Op::Csrrw, 1, 2, 0, 0x002}).value()) == "CSRRW R1, EPC, R2");
    CHECK(dis(encode({Op::Csrrsi, 0, 7, 0, 0x007}).value()) == "CSRRSI R0, IMASK, 7");
    CHECK(dis(encode({Op::Csrrs, 3, 0, 0, 0x123}).value()) == "CSRRS R3, 0x123, R0");
}

TEST_CASE("ABI register names are available on request") {
    CHECK(dis(encode({Op::Lw, 31, 29, 0, 8}).value(), RegisterNames::Abi) == "LW ra, 8(sp)");
    CHECK(dis(0x0002'08C1, RegisterNames::Abi) == "ADD v1, at, v0");
}

TEST_CASE("register names parse in both spellings, case-insensitively") {
    CHECK(parse_register("R0") == 0);
    CHECK(parse_register("r31") == 31);
    CHECK(parse_register("zero") == 0);
    CHECK(parse_register("SP") == 29);
    CHECK(parse_register("fp") == 30);
    CHECK(parse_register("s8") == 30);
    CHECK(parse_register("t9") == 27);
    CHECK_FALSE(parse_register("R32").has_value());
    CHECK_FALSE(parse_register("R05").has_value());
    CHECK_FALSE(parse_register("x5").has_value());
    CHECK_FALSE(parse_register("").has_value());
}

TEST_CASE("CSR lookup by number and by name") {
    CHECK(find_csr(u16{0x000})->name == "STATUS");
    CHECK(find_csr("cpuid")->csr == Csr::CpuId);
    CHECK_FALSE(find_csr(u16{0x009}).has_value());
    CHECK_FALSE(find_csr("MSTATUS").has_value());
    CHECK(find_csr("CPUID")->reset_value == kCpuIdValue);
}
