// asteria: command-line front end.
//
// Commands are added with the milestone that implements them (docs/ROADMAP.md). A command
// is listed only once it really works, so the CLI never has placeholder commands.
//
// Exit codes: 0 success, 2 bad usage or bad input.

#include <charconv>
#include <cstdio>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>

#include "common/types.hpp"
#include "isa/disassemble.hpp"
#include "isa/encoding.hpp"

namespace {

using namespace asteria;

constexpr std::string_view kVersion = ASTERIA_VERSION;
constexpr int kExitOk = 0;
constexpr int kExitUsage = 2;

void print_usage() {
    std::println("Asteria Computer Studio {} - command-line tools", kVersion);
    std::println("");
    std::println("usage: asteria <command> [arguments]");
    std::println("");
    std::println("commands:");
    std::println("  disasm <word>...   disassemble 32-bit machine words given in hex");
    std::println("                     (example: asteria disasm 000A0044 0x0002_08C1)");
    std::println("  help               show this message");
    std::println("  version            print the version");
}

// Parses a 32-bit hex word: optional "0x" prefix, optional '_' digit separators.
std::optional<u32> parse_hex_word(std::string_view text) {
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
    }
    std::string digits;
    for (const char c : text) {
        if (c != '_') {
            digits.push_back(c);
        }
    }
    if (digits.empty() || digits.size() > 8) {
        return std::nullopt;
    }
    u32 value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
    if (error != std::errc{} || end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return value;
}

int cmd_disasm(std::span<char* const> args) {
    if (args.empty()) {
        std::println(stderr, "asteria disasm: give one or more hex words (try 'asteria help')");
        return kExitUsage;
    }
    for (const std::string_view arg : args) {
        const auto word = parse_hex_word(arg);
        if (!word) {
            std::println(stderr, "asteria disasm: '{}' is not a 32-bit hex word", arg);
            return kExitUsage;
        }
        if (const auto in = isa::decode(*word)) {
            std::println("0x{:08X}  {}", *word, isa::disassemble(*in));
        } else {
            std::println("0x{:08X}  (illegal instruction)", *word);
        }
    }
    return kExitOk;
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::span<char* const> args{argv + 1, static_cast<std::size_t>(argc - 1)};

    if (args.empty()) {
        print_usage();
        return kExitOk;
    }

    const std::string_view command = args[0];
    const auto rest = args.subspan(1);
    if (command == "help" || command == "--help" || command == "-h") {
        print_usage();
        return kExitOk;
    }
    if (command == "version" || command == "--version") {
        std::println("{}", kVersion);
        return kExitOk;
    }
    if (command == "disasm") {
        return cmd_disasm(rest);
    }

    std::println(stderr, "asteria: unknown command '{}' (try 'asteria help')", command);
    return kExitUsage;
}
