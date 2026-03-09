#include "cli/parser.hpp"

#include <concepts>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

using namespace std::string_view_literals;

namespace {

bool has_flag(std::span<char*> args, std::string_view long_flag,
              std::string_view short_flag = "") {
    for (std::size_t i = 2; i < args.size(); ++i) {
        const std::string_view value(args[i]);
        if (value == long_flag ||
            (!short_flag.empty() && value == short_flag)) {
            return true;
        }
    }
    return false;
}

auto find_option_value(std::span<char*> args, std::string_view option)
    -> std::optional<std::string> {
    const auto prefix = std::string(option) + "=";
    for (std::size_t i = 2; i < args.size(); ++i) {
        const std::string_view value(args[i]);
        if (value == option) {
            if (i + 1 >= args.size()) {
                return std::nullopt;
            }
            return std::string(args[i + 1]);
        }
        if (value.starts_with(prefix)) {
            return std::string(value.substr(prefix.size()));
        }
    }
    return std::nullopt;
}

template <typename T>
concept HasRelease = requires(T t) {
    { t.release } -> std::convertible_to<bool>;
};

template <typename T>
concept HasTargetSelection = requires(T t) {
    { t.bin } -> std::convertible_to<std::optional<std::string>>;
    { t.example } -> std::convertible_to<std::optional<std::string>>;
};

auto require_named_option(std::span<char*> args, std::string_view option,
                          std::string_view usage)
    -> util::Result<std::optional<std::string>> {
    auto value{find_option_value(args, option)};

    if (!value) {
        return std::optional<std::string>{};
    }

    if (value->empty()) {
        return util::make_unexpected(
            std::format("the argument '{}' requires a value\n\nUsage: "
                        "{}\n\nFor more information, try '--help'.",
                        option, usage));
    }
    return value;
}

template <HasRelease CommandType>
auto parse_build_like(std::span<char*> args, std::string_view usage)
    -> util::Result<cli::ParsedCommand> {
    CommandType command{};
    command.release = has_flag(args, "--release"sv, "-r"sv);

    if constexpr (HasTargetSelection<CommandType>) {
        auto bin = require_named_option(args, "--bin", usage);
        if (!bin) {
            return std::unexpected(std::move(bin.error()));
        }
        auto example = require_named_option(args, "--example", usage);
        if (!example) {
            return std::unexpected(std::move(example.error()));
        }
        if (bin->has_value() && example->has_value()) {
            return util::make_unexpected(std::format(
                "the arguments '--bin' and '--example' cannot be used "
                "together\n\nUsage: {}\n\nFor more information, try '--help'.",
                usage));
        }
        command.bin = std::move(*bin);
        command.example = std::move(*example);
    }

    return util::Result<cli::ParsedCommand>{std::move(command)};
}

auto format_missing_required_args(std::string_view arg_name,
                                  std::string_view usage_line) -> std::string {
    return std::format(
        "the following required arguments were not provided:\n"
        "  {}\n\n"
        "Usage: {}\n\n"
        "For more information, try '--help'.",
        arg_name, usage_line);
}

auto format_unknown_command(std::string_view cmd) -> std::string {
    return std::format(
        "no such command: `{}`\n\n"
        "Usage: argo <COMMAND>\n\n"
        "For more information, try '--help'.",
        cmd);
}

auto require_option_value(std::span<char*> args, std::string_view option,
                          std::string_view usage) -> util::Result<std::string> {
    auto value = find_option_value(args, option);
    if (!value.has_value() || value->empty()) {
        return util::make_unexpected(std::format(
            "Missing required option '{}'\n\nUsage: {}", option, usage));
    }
    return *value;
}

}  // namespace

namespace cli {

auto parse(std::span<char*> args) -> util::Result<ParsedCommand> {
    if (args.size() < 2) {
        return HelpCommand{};
    }
    const std::string_view cmd(args[1]);

    if (cmd == "init"sv) {
        return InitCommand{};
    }

    if (cmd == "new"sv) {
        if (args.size() < 3) {
            return util::make_unexpected(
                format_missing_required_args("<PATH>", "argo new <PATH>"));
        }
        return NewCommand{.name = std::string(args[2])};
    }

    if (cmd == "add"sv) {
        if (args.size() < 3) {
            return util::make_unexpected(format_missing_required_args(
                "<DEP_SPEC>", "argo add <DEP_SPEC>"));
        }
        return AddCommand{.package = std::string(args[2])};
    }

    if (cmd == "remove"sv) {
        if (args.size() < 3) {
            return util::make_unexpected(
                format_missing_required_args("<DEP>", "argo remove <DEP>"));
        }
        return RemoveCommand{.package = std::string(args[2])};
    }

    if (cmd == "update"sv) {
        if (args.size() >= 3) {
            return UpdateCommand{.package = std::string(args[2])};
        }
        return UpdateCommand{};
    }

    if (cmd == "fetch"sv) {
        return FetchCommand{};
    }

    if (cmd == "publish"sv) {
        return PublishCommand{};
    }

    if (cmd == "yank"sv) {
        auto version = require_option_value(
            args, "--vers", "argo yank --vers <VERSION> [--undo]");
        if (!version) {
            return std::unexpected(std::move(version.error()));
        }
        return YankCommand{.version = *version,
                           .undo = has_flag(args, "--undo"sv)};
    }

    if (cmd == "owner"sv) {
        OwnerCommand owner;
        owner.add_owner = find_option_value(args, "--add");
        owner.remove_owner = find_option_value(args, "--remove");
        if (owner.add_owner.has_value() == owner.remove_owner.has_value()) {
            return util::make_unexpected(
                std::format("Usage Error: owner requires exactly one of --add "
                            "<OWNER> or --remove <OWNER>.\n\nUsage: {}",
                            "argo owner --add <OWNER>\n       argo owner "
                            "--remove <OWNER>"));
        }
        return owner;
    }

    if (cmd == "login"sv) {
        LoginCommand login;
        login.registry = find_option_value(args, "--registry").value_or("");
        login.token = find_option_value(args, "--token").value_or("");
        if (login.token.empty()) {
            return util::make_unexpected(std::format(
                "Usage Error: login requires --token <TOKEN>.\n\nUsage: {}",
                "argo login --token <TOKEN> [--registry <NAME>]"));
        }
        return login;
    }

    if (cmd == "logout"sv) {
        return LogoutCommand{
            .registry = find_option_value(args, "--registry").value_or("")};
    }

    if (cmd == "vcpkg"sv) {
        return VcpkgCommand{};
    }

    if (cmd == "ppargo"sv) {
        return PpargoCommand{};
    }

    if (cmd == "build"sv || cmd == "b"sv) {
        return parse_build_like<BuildCommand>(
            args, "argo build [--release] [--bin <NAME> | --example <NAME>]");
    }

    if (cmd == "check"sv || cmd == "c"sv) {
        return parse_build_like<CheckCommand>(
            args, "argo check [--release] [--bin <NAME> | --example <NAME>]");
    }

    if (cmd == "run"sv || cmd == "r"sv) {
        return parse_build_like<RunCommand>(
            args, "argo run [--release] [--bin <NAME> | --example <NAME>]");
    }

    if (cmd == "test"sv || cmd == "t"sv) {
        TestCommand test{};
        test.release = has_flag(args, "--release"sv, "-r"sv);

        const bool unit = has_flag(args, "--unit"sv);
        const bool integration = has_flag(args, "--integration"sv);
        if (unit && integration) {
            return util::make_unexpected(std::format(
                "the arguments '--unit' and '--integration' cannot be used "
                "together\n\nUsage: {}\n\nFor more information, try '--help'.",
                "argo test [--release] [--unit | --integration] [--test "
                "<NAME>]"));
        }

        auto test_name = require_named_option(
            args, "--test",
            "argo test [--release] [--unit | --integration] [--test <NAME>]");
        if (!test_name) {
            return std::unexpected(std::move(test_name.error()));
        }
        if (unit && test_name->has_value()) {
            return util::make_unexpected(std::format(
                "the arguments '--unit' and '--test' cannot be used "
                "together\n\nUsage: {}\n\nFor more information, try '--help'.",
                "argo test [--release] [--unit | --integration] [--test "
                "<NAME>]"));
        }

        if (unit) {
            test.scope = TestScope::Unit;
        } else if (integration || test_name->has_value()) {
            test.scope = TestScope::Integration;
        }
        test.test = std::move(*test_name);
        return test;
    }

    if (cmd == "version"sv) {
        return VersionCommand{};
    }

    if (cmd == "help"sv || cmd == "--help"sv || cmd == "-h"sv) {
        return HelpCommand{};
    }

    return util::make_unexpected(format_unknown_command(cmd));
}

}  // namespace cli
