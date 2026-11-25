// Every conformance program must pass, and its checks must be alive. A check is a line that
// branches to `fail`. Two kinds of "mutant" program are built, one change each, and every
// mutant must NOT pass:
//
//  1. Invert the check (BNE <-> BEQ, BLT <-> BGE, ...). A passing check's branch is not taken;
//     inverted, it is taken, so the mutant fails, IF the check runs at all. A survivor is a
//     check that never executes (a jump skips it).
//  2. Corrupt the result: flip bit 0 of the register an equality check tests (BNE r, x, fail /
//     BNEZ r, fail) just before the check. A real comparison now fails. A survivor compares
//     something that doesn't depend on r, such as `BNE t0, t0, fail`: a check that can never
//     fail. (Inversion can't find those: inverted, a tautology always fails.)
//
// A mutant must fail by RUNNING: it has to assemble, then not pass. A mutant that doesn't even
// build proves nothing, and counting it as "killed" once hid a bug in this very file (the
// corruption mutants were built from a dangling std::smatch and none of them assembled).
//
// Neither kind can tell whether an expected value is RIGHT: if the implementation and the test
// share a misreading of the spec, both agree and pass. That needs an independent derivation from
// the spec (see tests/isa/README.md).

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <fstream>
#include <iterator>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "isa_dir.hpp"
#include "machine/machine.hpp"
#include "toolchain/as/assembler.hpp"

using namespace asteria;

namespace {

constexpr u64 kMaxSteps = 100'000;
constexpr u32 kRam = 4u << 20;  // covers the kernel superpage that paging.asm maps

std::string read_program(const std::string& name) {
    const std::string path = std::string(kIsaDir) + "/" + name + ".asm";
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// "passes": assembles, loads, and halts with a0 = 0. "fails": assembles and loads, then anything
// else (a0 != 0, a trap loop, the step limit). "does not build": never got to run.
std::string run_program(const std::string& source) {
    const Result<toolchain::axf::Executable> exe = toolchain::as::assemble(source, "mutant.asm");
    if (!exe) return "does not build";
    std::unique_ptr<machine::Machine> m = machine::Machine::create({kRam}).value();
    if (!m->load(*exe)) return "does not build";
    const machine::RunReport r = m->run(kMaxSteps);
    return r.reason == machine::StopReason::Halted && r.exit_code == 0 ? "passes" : "fails";
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lines;
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) out += '\n';
        out += lines[i];
    }
    return out;
}

const char* inverse(const std::string& mnemonic) {
    static const std::pair<const char*, const char*> kPairs[] = {
        {"BEQ", "BNE"},   {"BNE", "BEQ"},   {"BEQZ", "BNEZ"}, {"BNEZ", "BEQZ"},
        {"BLT", "BGE"},   {"BGE", "BLT"},   {"BLTU", "BGEU"}, {"BGEU", "BLTU"},
        {"BGT", "BLE"},   {"BLE", "BGT"},   {"BGTU", "BLEU"}, {"BLEU", "BGTU"},
    };
    for (const auto& [from, to] : kPairs) {
        if (mnemonic == from) return to;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("Conformance: each program passes, and every mutant of a check fails") {
    // Every program in ISA_PROGRAMS (tests/isa/CMakeLists.txt), through the generated header.
    const std::string program = GENERATE(from_range(std::begin(kIsaPrograms), std::end(kIsaPrograms)));
    INFO("program: tests/isa/" << program << ".asm");

    const std::string source = read_program(program);
    REQUIRE(run_program(source) == "passes");

    // [label:] Bxx ..., fail      (group 3 starts with the first operand)
    static const std::regex kCheck(
        R"(^(\s*(?:[A-Za-z_.$][A-Za-z0-9_.$]*:)?\s*)(B[A-Z]+)(\s+([A-Za-z0-9]+)\s*,.*,?\s*fail\b.*)$)");
    std::vector<std::string> lines = split_lines(source);
    int inverted = 0;
    int corrupted = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::smatch m;
        if (!std::regex_match(lines[i], m, kCheck)) continue;
        // Copy the groups out NOW: `m` points into lines[i], which the mutants below overwrite.
        const std::string original = lines[i];
        const std::string prefix = m[1].str();
        const std::string mnemonic = m[2].str();
        const std::string operands = m[3].str();
        const std::string reg = m[4].str();
        INFO("line " << i + 1 << ": " << original);

        // 1. Inversion: the check must run.
        const char* flipped = inverse(mnemonic);
        REQUIRE(flipped != nullptr);
        lines[i] = prefix + flipped + operands;
        CHECK(run_program(join_lines(lines)) == "fails");
        lines[i] = original;
        ++inverted;

        // 2. Corruption: an equality check must depend on the register it tests. Checks on R0
        //    are exempt: they test that R0 stays 0, and R0 can't be corrupted.
        const bool equality = mnemonic == "BNE" || mnemonic == "BNEZ";
        if (equality && reg != "zero" && reg != "R0" && reg != "r0") {
            lines[i] = prefix + "XORI " + reg + ", " + reg + ", 1\n        " + mnemonic + operands;
            CHECK(run_program(join_lines(lines)) == "fails");
            lines[i] = original;
            ++corrupted;
        }
    }
    CHECK(inverted > 0);
    CHECK(corrupted > 0);
}
