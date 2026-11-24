// Every conformance program must pass, and every one of its checks must be able to fail.
//
// A check is a line that branches to `fail`. Inverting its condition (BNE <-> BEQ, ...) turns a
// passing check into a failing one, so each such "mutant" program must NOT pass. A mutant that
// still passes means the check never runs, or can never fail: a test that tests nothing.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <fstream>
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

// True when the program assembles, loads, and halts with a0 = 0.
bool passes(const std::string& source) {
    const Result<toolchain::axf::Executable> exe = toolchain::as::assemble(source, "mutant.asm");
    if (!exe) return false;
    std::unique_ptr<machine::Machine> m = machine::Machine::create({kRam}).value();
    if (!m->load(*exe)) return false;
    const machine::RunReport r = m->run(kMaxSteps);
    return r.reason == machine::StopReason::Halted && r.exit_code == 0;
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

TEST_CASE("Conformance: each program passes, and inverting any one check makes it fail") {
    const std::string program =
        GENERATE(as<std::string>{}, "alu", "alu_imm", "muldiv", "memory", "branch", "jump", "csr",
                 "traps", "paging");
    INFO("program: tests/isa/" << program << ".asm");

    const std::string source = read_program(program);
    REQUIRE(passes(source));

    // [label:] Bxx ..., fail
    static const std::regex kCheck(R"(^(\s*(?:[A-Za-z_.$][A-Za-z0-9_.$]*:)?\s*)(B[A-Z]+)(\s.*,\s*fail\b.*)$)");
    std::vector<std::string> lines = split_lines(source);
    int mutants = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::smatch m;
        if (!std::regex_match(lines[i], m, kCheck)) continue;
        const char* flipped = inverse(m[2].str());
        INFO("line " << i + 1 << ": " << lines[i]);
        REQUIRE(flipped != nullptr);

        const std::string original = lines[i];
        lines[i] = m[1].str() + flipped + m[3].str();
        CHECK_FALSE(passes(join_lines(lines)));
        lines[i] = original;
        ++mutants;
    }
    CHECK(mutants > 0);
}
