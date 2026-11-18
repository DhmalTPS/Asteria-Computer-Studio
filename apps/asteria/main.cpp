// asteria: command-line front end.
//
// Commands are added with the milestone that implements them (docs/ROADMAP.md). A command
// is listed only once it really works, so the CLI never has placeholder commands.

#include <cstdio>
#include <print>
#include <span>
#include <string_view>

namespace {

constexpr std::string_view kVersion = ASTERIA_VERSION;

void print_usage() {
    std::println("Asteria Computer Studio {} - command-line tools", kVersion);
    std::println("");
    std::println("usage: asteria <command> [arguments]");
    std::println("");
    std::println("commands:");
    std::println("  help       show this message");
    std::println("  version    print the version");
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::span<char*> args{argv + 1, static_cast<std::size_t>(argc - 1)};

    if (args.empty()) {
        print_usage();
        return 0;
    }

    const std::string_view command = args[0];
    if (command == "help" || command == "--help" || command == "-h") {
        print_usage();
        return 0;
    }
    if (command == "version" || command == "--version") {
        std::println("{}", kVersion);
        return 0;
    }

    std::println(stderr, "asteria: unknown command '{}' (try 'asteria help')", command);
    return 2;
}
