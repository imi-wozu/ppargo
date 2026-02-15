#include "cli/parser.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

namespace {
bool has_flag(std::span<char*> args, std::string_view long_flag,
              std::string_view short_flag) {
    return std::ranges::any_of(args | std::views::drop(2),
                               [long_flag, short_flag](const char* arg) {
                                   return std::string_view(arg) == long_flag ||
                                          std::string_view(arg) == short_flag;
                               });
}

template <typename T>
concept HasRelease = requires(T t) {
    { t.release } -> std::convertible_to<bool>;
};

template <HasRelease CommandType>
auto parse_build_like(std::span<char*> args) {
    return util::Result<cli::ParsedCommand>{
        CommandType{.release = has_flag(args, "--release"sv, "-r"sv)}};
}

auto format_missing_required_args(std::string_view arg_name,
                                  std::string_view usage_line) {
    return std::format(
        "the following required arguments were not provided:\n"
        "  {}\n\n"
        "Usage: {}\n\n"
        "For more information, try '--help'.",
        arg_name, usage_line);
}

auto format_unknown_command(std::string_view cmd) {
    return std::format(
        "no such command: `{}`\n\n"
        "Usage: argo <COMMAND>\n\n"
        "For more information, try '--help'.",
        cmd);
}

}  // namespace

namespace cli {

auto parse(int argc, char* argv[]) -> util::Result<ParsedCommand> {
    if (argc < 2) {
        return HelpCommand{};
    }

    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    const std::string_view cmd(args[1]);

    if (cmd == "init"sv) {
        return InitCommand{};
    }

    if (cmd == "new"sv) {
        if (argc < 3) {
            return std::unexpected(
                format_missing_required_args("<PATH>", "argo new <PATH>"));
        }
        return NewCommand{.name = std::string(args[2])};
    }

    if (cmd == "add"sv) {
        if (argc < 3) {
            return std::unexpected(format_missing_required_args(
                "<PACKAGE>", "argo add <PACKAGE>"));
        }
        return AddCommand{.package = std::string(args[2])};
    }

    if (cmd == "build"sv || cmd == "b"sv) {
        return parse_build_like<BuildCommand>(args);
    }

    if (cmd == "check"sv || cmd == "c"sv) {
        return parse_build_like<CheckCommand>(args);
    }

    if (cmd == "run"sv || cmd == "r"sv) {
        return parse_build_like<RunCommand>(args);
    }

    if (cmd == "version"sv) {
        return VersionCommand{};
    }

    if (cmd == "help"sv || cmd == "--help"sv || cmd == "-h"sv) {
        return HelpCommand{};
    }

    return std::unexpected(format_unknown_command(cmd));
}

}  // namespace cli
