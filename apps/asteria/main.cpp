// asteria: command-line front end.
//
// Commands are added with the milestone that implements them (docs/ROADMAP.md). A command
// is listed only once it really works, so the CLI never has placeholder commands.
//
// Exit codes:
//   0  success (for `run`: the program halted with a0 = 0)
//   1  `run`: the program halted with a0 != 0 (a self-checking test failed)
//   2  bad usage or bad input (unreadable file, assembly errors, invalid executable)
//   3  `run`: the program did not halt (step limit, trap loop, or waiting forever)

#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/result.hpp"
#include "common/types.hpp"
#include "isa/csrs.hpp"
#include "isa/disassemble.hpp"
#include "isa/encoding.hpp"
#include "isa/registers.hpp"
#include "isa/traps.hpp"
#include "machine/machine.hpp"
#include "toolchain/as/assembler.hpp"
#include "toolchain/as/listing.hpp"
#include "toolchain/axf/axf.hpp"

namespace {

using namespace asteria;
namespace axf = toolchain::axf;

constexpr std::string_view kVersion = ASTERIA_VERSION;
constexpr int kExitOk = 0;
constexpr int kExitTestFailed = 1;
constexpr int kExitUsage = 2;
constexpr int kExitAbnormal = 3;
constexpr u64 kDefaultMaxSteps = 10'000'000;

void print_usage() {
    std::println("Asteria Computer Studio {} - command-line tools", kVersion);
    std::println("");
    std::println("usage: asteria <command> [arguments]");
    std::println("");
    std::println("commands:");
    std::println("  build <file.asm> [-o <file.ast>]");
    std::println("                     assemble a program into an executable");
    std::println("  run <file.asm|file.ast> [--trace] [--max-steps N]");
    std::println("                     run a program on the reference interpreter and print the");
    std::println("                     registers; exit code 0 if it halts with a0 = 0, 1 if a0 != 0,");
    std::println("                     3 if it does not halt");
    std::println("  disasm <file.ast> [-o <file.asm>]");
    std::println("                     print (or write) an executable as assembly that reassembles");
    std::println("                     to the same bytes");
    std::println("  disasm <word>...   disassemble 32-bit machine words given in hex");
    std::println("                     (example: asteria disasm 000A0044 0x0002_08C1)");
    std::println("  help               show this message");
    std::println("  version            print the version");
}

// ---- files ----------------------------------------------------------------------------------

Result<std::vector<u8>> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail("can't open '{}'", path);
    std::vector<u8> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad()) return fail("can't read '{}'", path);
    return bytes;
}

Result<void> write_file(const std::string& path, std::span<const u8> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return fail("can't create '{}'", path);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) return fail("can't write '{}'", path);
    return {};
}

bool has_extension(std::string_view path, std::string_view ext) {
    return path.size() > ext.size() && path.ends_with(ext);
}

// .asm -> assemble; .ast -> read. Anything else is refused, so a typo can't be misread.
Result<axf::Executable> load_program(const std::string& path) {
    const bool is_asm = has_extension(path, ".asm");
    if (!is_asm && !has_extension(path, ".ast")) {
        return fail("'{}': expected a .asm or .ast file", path);
    }
    Result<std::vector<u8>> bytes = read_file(path);
    if (!bytes) return std::unexpected(bytes.error());
    if (!is_asm) {
        Result<axf::Executable> exe = axf::read(*bytes);
        if (!exe) return fail("{}: {}", path, exe.error().message);
        return exe;
    }
    const std::string source(bytes->begin(), bytes->end());
    return toolchain::as::assemble(source, path);
}

// ---- formatting ------------------------------------------------------------------------------

std::string describe_trap(const cpu::core::Trap& trap) {
    if (trap.is_interrupt()) return std::format("interrupt {}", trap.code());
    std::string text{isa::name(static_cast<isa::Exception>(trap.code()))};
    if (isa::is_page_fault(static_cast<isa::Exception>(trap.code()))) {
        static constexpr std::string_view kReasons[] = {"not present", "permission denied",
                                                        "misaligned superpage"};
        const u32 sub = (trap.cause >> isa::cause::kSubcodeLo) & 0xFF;
        text += std::format(" ({})", sub < 3 ? kReasons[sub] : "?");
    }
    if (trap.badvaddr) text += std::format(", BADVADDR = 0x{:08X}", *trap.badvaddr);
    return text;
}

void print_registers(const cpu::interp::Interpreter& cpu) {
    for (unsigned row = 0; row < 8; ++row) {
        std::string line;
        for (unsigned col = 0; col < 4; ++col) {
            const unsigned r = col * 8 + row;
            line += std::format("  {:>3} {:<4} {:08X}", isa::kNumericNames[r], isa::kAbiNames[r],
                                cpu.registers().read(r));
        }
        std::println("{}", line);
    }
    const auto& csr = cpu.csr_file();
    std::println("  pc 0x{:08X}   STATUS 0x{:X}   CAUSE 0x{:08X}   EPC 0x{:08X}   BADVADDR 0x{:08X}",
                 cpu.pc(), csr.status(), csr.read(static_cast<u16>(isa::Csr::Cause)),
                 csr.read(static_cast<u16>(isa::Csr::Epc)),
                 csr.read(static_cast<u16>(isa::Csr::BadVaddr)));
}

std::optional<u32> parse_hex_word(std::string_view text) {
    if (text.starts_with("0x") || text.starts_with("0X")) text.remove_prefix(2);
    std::string digits;
    for (const char c : text) {
        if (c != '_') digits.push_back(c);
    }
    if (digits.empty() || digits.size() > 8) return std::nullopt;
    u32 value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
    if (error != std::errc{} || end != digits.data() + digits.size()) return std::nullopt;
    return value;
}

// ---- commands -----------------------------------------------------------------------------------

int cmd_build(std::span<char* const> args) {
    std::optional<std::string> input;
    std::optional<std::string> output;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        if (a == "-o" && i + 1 < args.size()) {
            output = args[++i];
        } else if (!a.starts_with('-') && !input) {
            input = std::string{a};
        } else {
            std::println(stderr, "asteria build: unexpected argument '{}' (try 'asteria help')", a);
            return kExitUsage;
        }
    }
    if (!input || !has_extension(*input, ".asm")) {
        std::println(stderr, "asteria build: give one .asm file (try 'asteria help')");
        return kExitUsage;
    }
    if (!output) output = std::filesystem::path(*input).replace_extension(".ast").string();

    const Result<axf::Executable> exe = load_program(*input);
    if (!exe) {
        std::println(stderr, "{}", exe.error().message);
        return kExitUsage;
    }
    const Result<std::vector<u8>> bytes = axf::write(*exe);
    if (!bytes) {
        std::println(stderr, "asteria build: {}", bytes.error().message);
        return kExitUsage;
    }
    if (const Result<void> ok = write_file(*output, *bytes); !ok) {
        std::println(stderr, "asteria build: {}", ok.error().message);
        return kExitUsage;
    }
    std::println("wrote {} ({} bytes, {} segment{}, entry 0x{:08X})", *output, bytes->size(),
                 exe->segments.size(), exe->segments.size() == 1 ? "" : "s", exe->entry);
    return kExitOk;
}

int cmd_run(std::span<char* const> args) {
    std::optional<std::string> input;
    bool trace = false;
    u64 max_steps = kDefaultMaxSteps;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        if (a == "--trace") {
            trace = true;
        } else if (a == "--max-steps" && i + 1 < args.size()) {
            const std::string_view n = args[++i];
            const auto [end, error] = std::from_chars(n.data(), n.data() + n.size(), max_steps);
            if (error != std::errc{} || end != n.data() + n.size() || max_steps == 0) {
                std::println(stderr, "asteria run: --max-steps needs a positive number, got '{}'", n);
                return kExitUsage;
            }
        } else if (!a.starts_with('-') && !input) {
            input = std::string{a};
        } else {
            std::println(stderr, "asteria run: unexpected argument '{}' (try 'asteria help')", a);
            return kExitUsage;
        }
    }
    if (!input) {
        std::println(stderr, "asteria run: give a .asm or .ast file (try 'asteria help')");
        return kExitUsage;
    }

    const Result<axf::Executable> exe = load_program(*input);
    if (!exe) {
        std::println(stderr, "{}", exe.error().message);
        return kExitUsage;
    }
    Result<std::unique_ptr<machine::Machine>> m = machine::Machine::create({});
    if (!m) {
        std::println(stderr, "asteria run: {}", m.error().message);
        return kExitUsage;
    }
    if (const Result<void> ok = (*m)->load(*exe); !ok) {
        std::println(stderr, "asteria run: {}", ok.error().message);
        return kExitUsage;
    }

    u64 step = 0;
    const machine::StepObserver observer = [&](const cpu::interp::StepResult& r) {
        ++step;
        using cpu::interp::StepKind;
        const bool trapped = r.kind == StepKind::Trapped;
        const auto fetch_failed = [&] {
            const auto e = static_cast<isa::Exception>(r.trap->code());
            return e == isa::Exception::InstrBusError || e == isa::Exception::InstrPageFault;
        };
        std::string text;
        if (trapped && r.trap->is_interrupt()) {
            text = "(taken before this instruction)";
        } else if (trapped && fetch_failed()) {
            text = "(fetch failed)";
        } else if (const auto in = isa::decode(r.word)) {
            text = isa::disassemble(*in);
        } else {
            text = "(illegal instruction)";
        }
        std::println("{:>8}  {:08X}  {:08X}  {}", step, r.pc, r.word, text);
        if (r.kind == StepKind::Trapped) std::println("          trap: {}", describe_trap(*r.trap));
        if (r.kind == StepKind::Waiting) std::println("          waiting for an interrupt");
    };
    const machine::RunReport report = (*m)->run(max_steps, trace ? observer : machine::StepObserver{});
    const auto& cpu = (*m)->cpu();

    int exit_code = kExitAbnormal;
    switch (report.reason) {
        case machine::StopReason::Halted:
            std::println("halted with exit code {} after {} steps ({} instructions, {} traps)",
                         report.exit_code, report.steps, cpu.csr_file().retired(), report.traps);
            exit_code = report.exit_code == 0 ? kExitOk : kExitTestFailed;
            break;
        case machine::StopReason::StepLimit:
            std::println("stopped: the step limit of {} was reached (raise it with --max-steps)",
                         max_steps);
            break;
        case machine::StopReason::StuckTrap:
            std::println("stopped: stuck in a trap loop at 0x{:08X}: {}", report.last_trap_pc,
                         describe_trap(*report.last_trap));
            std::println("         the first trap was at 0x{:08X}: {}", report.first_trap_pc,
                         describe_trap(*report.first_trap));
            break;
        case machine::StopReason::WaitingForever:
            std::println("stopped: WFI at 0x{:08X} waits for an interrupt, and no device can raise one",
                         cpu.pc());
            break;
    }
    print_registers(cpu);
    return exit_code;
}

int cmd_disasm(std::span<char* const> args) {
    if (args.empty()) {
        std::println(stderr, "asteria disasm: give a .ast file or hex words (try 'asteria help')");
        return kExitUsage;
    }
    if (has_extension(args[0], ".ast")) {
        std::optional<std::string> output;
        if (args.size() == 3 && std::string_view{args[1]} == "-o") {
            output = args[2];
        } else if (args.size() != 1) {
            std::println(stderr, "asteria disasm: usage: asteria disasm <file.ast> [-o <file.asm>]");
            return kExitUsage;
        }
        const Result<axf::Executable> exe = load_program(args[0]);
        if (!exe) {
            std::println(stderr, "{}", exe.error().message);
            return kExitUsage;
        }
        const Result<std::string> text = toolchain::as::listing(*exe);
        if (!text) {
            std::println(stderr, "asteria disasm: {}", text.error().message);
            return kExitUsage;
        }
        if (!output) {
            std::print("{}", *text);
            return kExitOk;
        }
        const auto* data = reinterpret_cast<const u8*>(text->data());
        if (const Result<void> ok = write_file(*output, {data, text->size()}); !ok) {
            std::println(stderr, "asteria disasm: {}", ok.error().message);
            return kExitUsage;
        }
        std::println("wrote {}", *output);
        return kExitOk;
    }
    for (const std::string_view arg : args) {
        const auto word = parse_hex_word(arg);
        if (!word) {
            std::println(stderr, "asteria disasm: '{}' is not a 32-bit hex word or a .ast file", arg);
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
        // No command is a usage error, like an unknown one: a script must not mistake it for
        // success. `asteria help` asks for the text and exits 0.
        print_usage();
        return kExitUsage;
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
    if (command == "build") return cmd_build(rest);
    if (command == "run") return cmd_run(rest);
    if (command == "disasm") return cmd_disasm(rest);

    std::println(stderr, "asteria: unknown command '{}' (try 'asteria help')", command);
    return kExitUsage;
}
