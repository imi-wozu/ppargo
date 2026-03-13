#include "cli/parser.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

using namespace std::string_view_literals;

namespace {

auto tokens(std::span<char*> args, std::size_t start_index = 1) {
    return args.subspan(start_index) | std::views::transform([](char* arg) {
               return std::string_view(arg);
           });
}

bool has_flag(std::span<char*> args, std::string_view long_flag,
              std::string_view short_flag = "") {
    return std::ranges::any_of(tokens(args), [&](std::string_view value) {
        return value == long_flag ||
               (!short_flag.empty() && value == short_flag);
    });
}

bool has_help_flag(std::span<char*> args) {
    const auto help_tokens =
        tokens(args) | std::views::take_while([](std::string_view token) {
            return token != "--"sv;
        });
    return std::ranges::any_of(help_tokens, [](std::string_view token) {
        return token == "--help"sv || token == "-h"sv;
    });
}

auto is_quiet_token(std::string_view token) -> bool {
    return token == "-q"sv || token == "--quiet"sv;
}

auto non_quiet_tokens(std::span<char*> args, std::size_t start_index = 1) {
    return tokens(args, start_index) |
           std::views::filter(
               [](std::string_view token) { return !is_quiet_token(token); });
}

auto first_non_quiet_token(std::span<char*> args, std::size_t start_index = 1)
    -> std::optional<std::string_view> {
    auto tokens = non_quiet_tokens(args, start_index);
    const auto it = tokens.begin();
    if (it == tokens.end()) {
        return std::nullopt;
    }
    return *it;
}

bool has_option_token(std::span<char*> args, std::string_view option) {
    const auto prefix = std::string(option) + "=";
    return std::ranges::any_of(tokens(args), [&](std::string_view value) {
        return value == option || value.starts_with(prefix);
    });
}

auto find_option_value(std::span<char*> args, std::string_view option)
    -> std::optional<std::string> {
    const auto prefix = std::string(option) + "=";
    for (std::size_t i = 1; i < args.size(); ++i) {
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

auto read_option_value(std::span<char*> args, std::size_t& index,
                       std::string_view option) -> util::Result<std::string> {
    const std::string_view value(args[index]);
    const auto prefix = std::string(option) + "=";
    if (value.starts_with(prefix)) {
        const auto inline_value = std::string(value.substr(prefix.size()));
        if (inline_value.empty()) {
            return util::make_unexpected(
                std::format("the argument '{}' requires a value", option));
        }
        return inline_value;
    }
    if (index + 1 >= args.size()) {
        return util::make_unexpected(
            std::format("the argument '{}' requires a value", option));
    }
    ++index;
    const std::string next(args[index]);
    if (next.empty()) {
        return util::make_unexpected(
            std::format("the argument '{}' requires a value", option));
    }
    return next;
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
        "For more information, try '--help'.",
        cmd);
}

auto unexpected_argument_error(std::string_view value) -> util::Error {
    return util::make_error(std::format("unexpected argument '{}'", value));
}

auto require_option_value(std::span<char*> args, std::string_view option,
                          std::string_view usage) -> util::Result<std::string> {
    if (!has_option_token(args, option)) {
        return util::make_unexpected(std::format(
            "Missing required option '{}'\n\nUsage: {}", option, usage));
    }
    auto value = find_option_value(args, option);
    if (!value.has_value() || value->empty()) {
        return util::make_unexpected(std::format(
            "Missing required option '{}'\n\nUsage: {}", option, usage));
    }
    return *value;
}

struct GlobalOptions {
    bool quiet = false;
    bool help = false;
    bool version = false;
    bool verbose_version = false;
};

struct GlobalParseResult {
    GlobalOptions options;
    std::size_t command_index = 0;
};

auto global_usage() -> std::string_view { return "argo [OPTIONS] [COMMAND]"; }
auto help_usage() -> std::string_view { return "argo help [COMMAND]"; }
auto version_usage() -> std::string_view { return "argo version [--verbose]"; }

auto parse_positive_int(std::string_view text, std::string_view option)
    -> util::Result<int> {
    int parsed = 0;
    auto [ptr, ec] =
        std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (ec != std::errc{} || ptr != text.data() + text.size() || parsed <= 0) {
        return util::make_unexpected(std::format(
            "the argument '{}' requires a positive integer value", option));
    }
    return parsed;
}

auto parse_global_options(std::span<char*> args)
    -> util::Result<GlobalParseResult> {
    GlobalParseResult result{};
    result.command_index = args.size();

    bool seen_subcommand = false;
    bool passthrough = false;

    for (const auto i : std::views::iota(std::size_t{1}, args.size())) {
        const std::string_view token(args[i]);

        if (passthrough) {
            continue;
        }

        if (seen_subcommand && token == "--"sv) {
            passthrough = true;
            continue;
        }

        if (!seen_subcommand) {
            if (is_quiet_token(token)) {
                result.options.quiet = true;
                continue;
            }
            if (token == "-h"sv || token == "--help"sv) {
                result.options.help = true;
                continue;
            }
            if (token == "-Vv"sv) {
                result.options.version = true;
                result.options.verbose_version = true;
                continue;
            }
            if (token == "-V"sv || token == "--version"sv) {
                result.options.version = true;
                continue;
            }
            if (!token.empty() && token.front() == '-') {
                return util::make_unexpected(
                    std::format("unexpected option '{}'\n\nUsage: {}\n\nFor "
                                "more information, try '--help'.",
                                token, global_usage()));
            }

            seen_subcommand = true;
            result.command_index = i;
            continue;
        }

        if (is_quiet_token(token)) {
            result.options.quiet = true;
        }
    }

    return result;
}

template <typename T>
concept HasQuiet = requires(T command) {
    { command.quiet } -> std::convertible_to<bool&>;
};

template <typename CommandType>
void apply_global_options(CommandType& command, const GlobalOptions& options) {
    if constexpr (HasQuiet<CommandType>) {
        command.quiet = command.quiet || options.quiet;
    }
}

auto apply_global_options(cli::ParsedCommand command,
                          const GlobalOptions& options) -> cli::ParsedCommand {
    std::visit(
        [&](auto& parsed_command) {
            apply_global_options(parsed_command, options);
        },
        command);
    return command;
}

enum class OptionAction {
    Release,
    Help,
    Verbose,
    VerboseDouble,
    Bin,
    BinsAll,
    Example,
    ExamplesAll,
    Test,
    TestsAll,
    Bench,
    BenchesAll,
    AllTargets,
    ManifestPath,
    TargetDir,
    Jobs,
    Locked,
    Offline,
    Frozen,
    Profile,
    Color,
    KeepGoing,
    NoRun,
    NoFailFast,
    Unit,
    Integration,
};

struct OptionSpec {
    OptionAction action;
    std::string_view long_name;
    std::string_view short_name{};
    bool takes_value = false;
};

struct UnsupportedOptionSpec {
    std::string_view name;
    bool takes_value = false;
};

struct CommandSpec {
    std::string_view command_name;
    std::string_view usage_line;
    cli::HelpTopic help_topic;
    std::span<const OptionSpec> options;
    std::span<const UnsupportedOptionSpec> unsupported_options;
    bool allow_passthrough = false;
    bool profile_test_is_unsupported = false;
};

struct BuildLikeParseState {
    bool saw_release_flag = false;
    std::optional<std::string> profile;
};

auto make_usage_error_value(std::string_view usage_line,
                            std::string_view message) -> util::Error {
    return util::make_error(
        std::format("{}\n\nUsage: {}\n\nFor more information, try '--help'.",
                    message, usage_line));
}

auto make_usage_error_value(const CommandSpec& spec, std::string_view message)
    -> util::Error {
    return make_usage_error_value(spec.usage_line, message);
}

auto make_usage_error(std::string_view usage_line, std::string_view message)
    -> util::Result<cli::ParsedCommand> {
    return std::unexpected(make_usage_error_value(usage_line, message));
}

auto make_usage_error(const CommandSpec& spec, std::string_view message)
    -> util::Result<cli::ParsedCommand> {
    return make_usage_error(spec.usage_line, message);
}

auto make_unsupported_error(const CommandSpec& spec, std::string_view option)
    -> util::Result<cli::ParsedCommand> {
    return util::make_unexpected(std::format(
        "{} option '{}' is not supported yet in ppargo.\n\nUsage: {}\n\nFor "
        "more information, try '--help'.",
        spec.command_name, option, spec.usage_line));
}

constexpr std::array kBuildOptions{
    OptionSpec{OptionAction::Release, "--release", "-r"},
    OptionSpec{OptionAction::Help, "--help", "-h"},
    OptionSpec{OptionAction::Verbose, "--verbose", "-v"},
    OptionSpec{OptionAction::VerboseDouble, "-vv"},
    OptionSpec{OptionAction::BinsAll, "--bins"},
    OptionSpec{OptionAction::ExamplesAll, "--examples"},
    OptionSpec{OptionAction::TestsAll, "--tests"},
    OptionSpec{OptionAction::AllTargets, "--all-targets"},
    OptionSpec{OptionAction::Locked, "--locked"},
    OptionSpec{OptionAction::Offline, "--offline"},
    OptionSpec{OptionAction::Frozen, "--frozen"},
    OptionSpec{OptionAction::Bin, "--bin", "", true},
    OptionSpec{OptionAction::Example, "--example", "", true},
    OptionSpec{OptionAction::Test, "--test", "", true},
    OptionSpec{OptionAction::ManifestPath, "--manifest-path", "", true},
    OptionSpec{OptionAction::TargetDir, "--target-dir", "", true},
    OptionSpec{OptionAction::Jobs, "--jobs", "-j", true},
    OptionSpec{OptionAction::Profile, "--profile", "", true},
    OptionSpec{OptionAction::Color, "--color", "", true},
};

constexpr std::array kBuildUnsupportedOptions{
    UnsupportedOptionSpec{"-p"},
    UnsupportedOptionSpec{"--package", true},
    UnsupportedOptionSpec{"--workspace"},
    UnsupportedOptionSpec{"--all"},
    UnsupportedOptionSpec{"--exclude", true},
    UnsupportedOptionSpec{"-F"},
    UnsupportedOptionSpec{"--features", true},
    UnsupportedOptionSpec{"--all-features"},
    UnsupportedOptionSpec{"--no-default-features"},
    UnsupportedOptionSpec{"--lib"},
    UnsupportedOptionSpec{"--bench", true},
    UnsupportedOptionSpec{"--benches"},
    UnsupportedOptionSpec{"--target", true},
    UnsupportedOptionSpec{"--message-format", true},
    UnsupportedOptionSpec{"--artifact-dir", true},
    UnsupportedOptionSpec{"--ignore-rust-version"},
    UnsupportedOptionSpec{"--config", true},
    UnsupportedOptionSpec{"-C"},
    UnsupportedOptionSpec{"-Z"},
    UnsupportedOptionSpec{"--future-incompat-report"},
    UnsupportedOptionSpec{"--keep-going"},
};

constexpr std::array kCheckOptions{
    OptionSpec{OptionAction::Release, "--release", "-r"},
    OptionSpec{OptionAction::Help, "--help", "-h"},
    OptionSpec{OptionAction::Verbose, "--verbose", "-v"},
    OptionSpec{OptionAction::VerboseDouble, "-vv"},
    OptionSpec{OptionAction::BinsAll, "--bins"},
    OptionSpec{OptionAction::ExamplesAll, "--examples"},
    OptionSpec{OptionAction::TestsAll, "--tests"},
    OptionSpec{OptionAction::BenchesAll, "--benches"},
    OptionSpec{OptionAction::AllTargets, "--all-targets"},
    OptionSpec{OptionAction::Locked, "--locked"},
    OptionSpec{OptionAction::Offline, "--offline"},
    OptionSpec{OptionAction::Frozen, "--frozen"},
    OptionSpec{OptionAction::KeepGoing, "--keep-going"},
    OptionSpec{OptionAction::Bin, "--bin", "", true},
    OptionSpec{OptionAction::Example, "--example", "", true},
    OptionSpec{OptionAction::Test, "--test", "", true},
    OptionSpec{OptionAction::Bench, "--bench", "", true},
    OptionSpec{OptionAction::ManifestPath, "--manifest-path", "", true},
    OptionSpec{OptionAction::TargetDir, "--target-dir", "", true},
    OptionSpec{OptionAction::Jobs, "--jobs", "-j", true},
    OptionSpec{OptionAction::Profile, "--profile", "", true},
    OptionSpec{OptionAction::Color, "--color", "", true},
};

constexpr std::array kCheckUnsupportedOptions{
    UnsupportedOptionSpec{"-p"},
    UnsupportedOptionSpec{"--package", true},
    UnsupportedOptionSpec{"--workspace"},
    UnsupportedOptionSpec{"--all"},
    UnsupportedOptionSpec{"--exclude", true},
    UnsupportedOptionSpec{"-F"},
    UnsupportedOptionSpec{"--features", true},
    UnsupportedOptionSpec{"--all-features"},
    UnsupportedOptionSpec{"--no-default-features"},
    UnsupportedOptionSpec{"--lib"},
    UnsupportedOptionSpec{"--target", true},
    UnsupportedOptionSpec{"--message-format", true},
    UnsupportedOptionSpec{"--timings"},
    UnsupportedOptionSpec{"--ignore-rust-version"},
    UnsupportedOptionSpec{"--config", true},
    UnsupportedOptionSpec{"-C"},
    UnsupportedOptionSpec{"-Z"},
    UnsupportedOptionSpec{"--future-incompat-report"},
};

constexpr std::array kRunOptions{
    OptionSpec{OptionAction::Release, "--release", "-r"},
    OptionSpec{OptionAction::Help, "--help", "-h"},
    OptionSpec{OptionAction::Bin, "--bin", "", true},
    OptionSpec{OptionAction::Example, "--example", "", true},
};

constexpr std::array kRunUnsupportedOptions{
    UnsupportedOptionSpec{"-v"},
    UnsupportedOptionSpec{"-vv"},
    UnsupportedOptionSpec{"--verbose"},
    UnsupportedOptionSpec{"--locked"},
    UnsupportedOptionSpec{"--offline"},
    UnsupportedOptionSpec{"--frozen"},
    UnsupportedOptionSpec{"--manifest-path", true},
    UnsupportedOptionSpec{"--target-dir", true},
    UnsupportedOptionSpec{"-j"},
    UnsupportedOptionSpec{"--jobs", true},
    UnsupportedOptionSpec{"--profile", true},
    UnsupportedOptionSpec{"--color", true},
    UnsupportedOptionSpec{"-p"},
    UnsupportedOptionSpec{"--package", true},
    UnsupportedOptionSpec{"--workspace"},
    UnsupportedOptionSpec{"--all"},
    UnsupportedOptionSpec{"--exclude", true},
    UnsupportedOptionSpec{"-F"},
    UnsupportedOptionSpec{"--features", true},
    UnsupportedOptionSpec{"--all-features"},
    UnsupportedOptionSpec{"--no-default-features"},
    UnsupportedOptionSpec{"--target", true},
    UnsupportedOptionSpec{"--message-format", true},
    UnsupportedOptionSpec{"--ignore-rust-version"},
    UnsupportedOptionSpec{"--config", true},
    UnsupportedOptionSpec{"-C"},
    UnsupportedOptionSpec{"-Z"},
    UnsupportedOptionSpec{"--future-incompat-report"},
};

constexpr std::array kTestOptions{
    OptionSpec{OptionAction::Release, "--release", "-r"},
    OptionSpec{OptionAction::Help, "--help", "-h"},
    OptionSpec{OptionAction::Verbose, "--verbose", "-v"},
    OptionSpec{OptionAction::VerboseDouble, "-vv"},
    OptionSpec{OptionAction::NoRun, "--no-run"},
    OptionSpec{OptionAction::NoFailFast, "--no-fail-fast"},
    OptionSpec{OptionAction::Unit, "--unit"},
    OptionSpec{OptionAction::Integration, "--integration"},
    OptionSpec{OptionAction::TestsAll, "--tests"},
    OptionSpec{OptionAction::ExamplesAll, "--examples"},
    OptionSpec{OptionAction::BenchesAll, "--benches"},
    OptionSpec{OptionAction::AllTargets, "--all-targets"},
    OptionSpec{OptionAction::Locked, "--locked"},
    OptionSpec{OptionAction::Offline, "--offline"},
    OptionSpec{OptionAction::Frozen, "--frozen"},
    OptionSpec{OptionAction::Test, "--test", "", true},
    OptionSpec{OptionAction::Example, "--example", "", true},
    OptionSpec{OptionAction::Bench, "--bench", "", true},
    OptionSpec{OptionAction::ManifestPath, "--manifest-path", "", true},
    OptionSpec{OptionAction::TargetDir, "--target-dir", "", true},
    OptionSpec{OptionAction::Jobs, "--jobs", "-j", true},
    OptionSpec{OptionAction::Profile, "--profile", "", true},
    OptionSpec{OptionAction::Color, "--color", "", true},
};

constexpr std::array kTestUnsupportedOptions{
    UnsupportedOptionSpec{"-p"},
    UnsupportedOptionSpec{"--package", true},
    UnsupportedOptionSpec{"--workspace"},
    UnsupportedOptionSpec{"--all"},
    UnsupportedOptionSpec{"--exclude", true},
    UnsupportedOptionSpec{"-F"},
    UnsupportedOptionSpec{"--features", true},
    UnsupportedOptionSpec{"--all-features"},
    UnsupportedOptionSpec{"--no-default-features"},
    UnsupportedOptionSpec{"--lib"},
    UnsupportedOptionSpec{"--bin"},
    UnsupportedOptionSpec{"--bins"},
    UnsupportedOptionSpec{"--doc"},
    UnsupportedOptionSpec{"--target", true},
    UnsupportedOptionSpec{"--message-format", true},
    UnsupportedOptionSpec{"--ignore-rust-version"},
    UnsupportedOptionSpec{"--config", true},
    UnsupportedOptionSpec{"-C"},
    UnsupportedOptionSpec{"-Z"},
    UnsupportedOptionSpec{"--future-incompat-report"},
};

const CommandSpec kBuildCommandSpec{"build",
                                    "argo build [options]",
                                    cli::HelpTopic::Build,
                                    std::span{kBuildOptions},
                                    std::span{kBuildUnsupportedOptions},
                                    false,
                                    false};

const CommandSpec kCheckCommandSpec{"check",
                                    "argo check [options]",
                                    cli::HelpTopic::Check,
                                    std::span{kCheckOptions},
                                    std::span{kCheckUnsupportedOptions},
                                    false,
                                    true};

const CommandSpec kRunCommandSpec{"run",
                                  "argo run [--release] [--bin <NAME> | "
                                  "--example <NAME>]",
                                  cli::HelpTopic::Run,
                                  std::span{kRunOptions},
                                  std::span{kRunUnsupportedOptions},
                                  false,
                                  false};

const CommandSpec kTestCommandSpec{"test",
                                   "argo test [options] [testname] "
                                   "[-- test-options]",
                                   cli::HelpTopic::Test,
                                   std::span{kTestOptions},
                                   std::span{kTestUnsupportedOptions},
                                   true,
                                   false};

auto has_inline_option_value(std::string_view token, std::string_view option)
    -> bool {
    return token.size() > option.size() && token.starts_with(option) &&
           token[option.size()] == '=';
}

auto matches_option(const OptionSpec& option, std::string_view token) -> bool {
    if (token == option.long_name ||
        (!option.short_name.empty() && token == option.short_name)) {
        return true;
    }
    return option.takes_value &&
           has_inline_option_value(token, option.long_name);
}

auto matches_unsupported_option(const UnsupportedOptionSpec& option,
                                std::string_view token) -> bool {
    if (token == option.name) {
        return true;
    }
    return option.takes_value && has_inline_option_value(token, option.name);
}

auto find_matching_option(std::span<const OptionSpec> options,
                          std::string_view token) -> const OptionSpec* {
    const auto found =
        std::ranges::find_if(options, [&](const OptionSpec& option) {
            return matches_option(option, token);
        });
    return found == options.end() ? nullptr : &*found;
}

auto is_unsupported_option(std::span<const UnsupportedOptionSpec> options,
                           std::string_view token) -> bool {
    return std::ranges::any_of(
        options, [&](const UnsupportedOptionSpec& option) {
            return matches_unsupported_option(option, token);
        });
}

auto matched_option_name(const OptionSpec& option, std::string_view token)
    -> std::string_view {
    if (!option.short_name.empty() && token == option.short_name) {
        return option.short_name;
    }
    return option.long_name;
}

auto set_jobs(std::optional<int>& jobs, std::string_view value,
              std::string_view option) -> util::Status {
    auto parsed_jobs = parse_positive_int(value, option);
    if (!parsed_jobs) {
        return std::unexpected(std::move(parsed_jobs.error()));
    }
    jobs = *parsed_jobs;
    return util::Ok;
}

auto set_color(std::string& color, std::string_view value) -> util::Status {
    if (value != "auto" && value != "always" && value != "never") {
        return std::unexpected(util::make_error(std::format(
            "invalid value '{}' for '--color': expected 'auto', 'always', or "
            "'never'",
            value)));
    }
    color = std::string(value);
    return util::Ok;
}

auto set_profile(BuildLikeParseState& state, std::string value)
    -> util::Status {
    state.profile = std::move(value);
    return util::Ok;
}

auto finalize_profile(bool& release, const BuildLikeParseState& state)
    -> util::Status {
    release = state.saw_release_flag;
    if (!state.profile.has_value()) {
        return util::Ok;
    }
    if (*state.profile == "dev") {
        if (state.saw_release_flag) {
            return std::unexpected(util::make_error(
                "the arguments '--release' and '--profile dev' cannot be used "
                "together"));
        }
        release = false;
        return util::Ok;
    }
    if (*state.profile == "release") {
        release = true;
        return util::Ok;
    }
    return std::unexpected(util::make_error(std::format(
        "invalid value '{}' for '--profile': expected 'dev' or 'release'",
        *state.profile)));
}

template <typename T>
concept HasRelease = requires(T command) {
    { command.release } -> std::convertible_to<bool&>;
};

template <typename T>
concept HasQuietAndVerbose = requires(T command) {
    { command.quiet } -> std::convertible_to<bool&>;
    { command.verbose } -> std::convertible_to<int&>;
};

template <typename T>
concept HasDependencyWorkflow = requires(T command) {
    { command.frozen } -> std::convertible_to<bool&>;
    { command.locked } -> std::convertible_to<bool&>;
    { command.offline } -> std::convertible_to<bool&>;
};

template <typename T>
concept HasInvocationOverrides = requires(T command) {
    { command.manifest_path };
    { command.target_dir };
    { command.jobs };
    { command.color };
};

template <typename T>
concept HasBins = requires(T command) {
    { command.bins };
    { command.bins_all } -> std::convertible_to<bool&>;
};

template <typename T>
concept HasExamples = requires(T command) {
    { command.examples };
    { command.examples_all } -> std::convertible_to<bool&>;
};

template <typename T>
concept HasTests = requires(T command) {
    { command.tests };
    { command.tests_all } -> std::convertible_to<bool&>;
};

template <typename T>
concept HasBenches = requires(T command) {
    { command.benches };
    { command.benches_all } -> std::convertible_to<bool&>;
};

template <typename T>
concept HasKeepGoing = requires(T command) {
    { command.keep_going } -> std::convertible_to<bool&>;
};

template <typename T>
concept HasNoRun = requires(T command) {
    { command.no_run } -> std::convertible_to<bool&>;
};

template <typename T>
concept HasNoFailFast = requires(T command) {
    { command.no_fail_fast } -> std::convertible_to<bool&>;
};

template <typename T>
concept HasTestScope = requires(T command) {
    { command.scope } -> std::convertible_to<cli::TestScope&>;
};

template <HasRelease T>
auto apply_common_option(
    T& command, BuildLikeParseState& state, const OptionSpec& option,
    [[maybe_unused]] const std::optional<std::string>& value,
    std::string_view option_name) -> util::Status {
    switch (option.action) {
        case OptionAction::Release:
            state.saw_release_flag = true;
            return util::Ok;
        case OptionAction::Verbose:
            if constexpr (HasQuietAndVerbose<T>) {
                ++command.verbose;
                return util::Ok;
            }
            break;
        case OptionAction::VerboseDouble:
            if constexpr (HasQuietAndVerbose<T>) {
                command.verbose += 2;
                return util::Ok;
            }
            break;
        default:
            break;
    }
    return std::unexpected(
        util::make_error(std::format("unexpected option '{}'", option_name)));
}

template <typename T>
auto apply_shared_build_like_option(
    T& command, BuildLikeParseState& state, const OptionSpec& option,
    [[maybe_unused]] const std::optional<std::string>& value,
    std::string_view option_name) -> util::Status {
    switch (option.action) {
        case OptionAction::Bin:
            if constexpr (HasBins<T>) {
                command.bins.push_back(*value);
                return util::Ok;
            } else if constexpr (std::same_as<T, cli::RunCommand>) {
                if (!command.bin.has_value()) {
                    command.bin = *value;
                }
                return util::Ok;
            }
            break;
        case OptionAction::BinsAll:
            if constexpr (HasBins<T>) {
                command.bins_all = true;
                return util::Ok;
            }
            break;
        case OptionAction::Example:
            if constexpr (HasExamples<T>) {
                command.examples.push_back(*value);
                return util::Ok;
            } else if constexpr (std::same_as<T, cli::RunCommand>) {
                if (!command.example.has_value()) {
                    command.example = *value;
                }
                return util::Ok;
            }
            break;
        case OptionAction::ExamplesAll:
            if constexpr (HasExamples<T>) {
                command.examples_all = true;
                return util::Ok;
            }
            break;
        case OptionAction::Test:
            if constexpr (HasTests<T>) {
                command.tests.push_back(*value);
                return util::Ok;
            }
            break;
        case OptionAction::TestsAll:
            if constexpr (HasTests<T>) {
                command.tests_all = true;
                return util::Ok;
            }
            break;
        case OptionAction::Bench:
            if constexpr (HasBenches<T>) {
                command.benches.push_back(*value);
                return util::Ok;
            }
            break;
        case OptionAction::BenchesAll:
            if constexpr (HasBenches<T>) {
                command.benches_all = true;
                return util::Ok;
            }
            break;
        case OptionAction::AllTargets:
            if constexpr (HasBins<T>) {
                command.bins_all = true;
            }
            if constexpr (HasExamples<T>) {
                command.examples_all = true;
            }
            if constexpr (HasTests<T>) {
                command.tests_all = true;
            }
            if constexpr (HasBenches<T>) {
                command.benches_all = true;
            }
            return util::Ok;
        case OptionAction::KeepGoing:
            if constexpr (HasKeepGoing<T>) {
                command.keep_going = true;
                return util::Ok;
            }
            break;
        case OptionAction::ManifestPath:
            if constexpr (HasInvocationOverrides<T>) {
                command.manifest_path = *value;
                return util::Ok;
            }
            break;
        case OptionAction::TargetDir:
            if constexpr (HasInvocationOverrides<T>) {
                command.target_dir = *value;
                return util::Ok;
            }
            break;
        case OptionAction::Jobs:
            if constexpr (HasInvocationOverrides<T>) {
                return set_jobs(command.jobs, *value, option_name);
            }
            break;
        case OptionAction::Locked:
            if constexpr (HasDependencyWorkflow<T>) {
                command.locked = true;
                return util::Ok;
            }
            break;
        case OptionAction::Offline:
            if constexpr (HasDependencyWorkflow<T>) {
                command.offline = true;
                return util::Ok;
            }
            break;
        case OptionAction::Frozen:
            if constexpr (HasDependencyWorkflow<T>) {
                command.frozen = true;
                return util::Ok;
            }
            break;
        case OptionAction::Profile:
            return set_profile(state, *value);
        case OptionAction::Color:
            if constexpr (HasInvocationOverrides<T>) {
                return set_color(command.color, *value);
            }
            break;
        case OptionAction::NoRun:
            if constexpr (HasNoRun<T>) {
                command.no_run = true;
                return util::Ok;
            }
            break;
        case OptionAction::NoFailFast:
            if constexpr (HasNoFailFast<T>) {
                command.no_fail_fast = true;
                return util::Ok;
            }
            break;
        case OptionAction::Unit:
            if constexpr (HasTestScope<T>) {
                if (command.scope == cli::TestScope::Integration) {
                    return std::unexpected(util::make_error(
                        "the arguments '--unit' and '--integration' cannot be "
                        "used together"));
                }
                command.scope = cli::TestScope::Unit;
                return util::Ok;
            }
            break;
        case OptionAction::Integration:
            if constexpr (HasTestScope<T>) {
                if (command.scope == cli::TestScope::Unit) {
                    return std::unexpected(util::make_error(
                        "the arguments '--unit' and '--integration' cannot be "
                        "used together"));
                }
                command.scope = cli::TestScope::Integration;
                return util::Ok;
            }
            break;
        default:
            break;
    }

    return apply_common_option(command, state, option, value, option_name);
}

template <typename CommandType>
    requires(!std::same_as<CommandType, cli::TestCommand>)
auto handle_positional_argument(CommandType&, std::string_view value)
    -> util::Status {
    return std::unexpected(unexpected_argument_error(value));
}

auto handle_positional_argument(cli::TestCommand& command,
                                std::string_view value) -> util::Status {
    if (!command.test_filter.has_value()) {
        command.test_filter = std::string(value);
        return util::Ok;
    }
    return std::unexpected(unexpected_argument_error(value));
}

template <HasRelease T>
auto finalize_profile_base(T& command, const BuildLikeParseState& state)
    -> util::Status {
    GUARD(finalize_profile(command.release, state));
    return util::Ok;
}

template <typename CommandType>
consteval auto validate_shared_option_contracts() -> void {
    if constexpr (std::same_as<CommandType, cli::BuildCommand>) {
        static_assert(!HasBenches<CommandType>,
                      "BuildCommand must not expose bench selector fields");
    }
    if constexpr (std::same_as<CommandType, cli::TestCommand>) {
        static_assert(!HasBins<CommandType>,
                      "TestCommand must not expose bin selector fields");
    }
}

template <typename T>
    requires HasRelease<T> && HasQuietAndVerbose<T> && HasDependencyWorkflow<T>
auto finalize_build_like_base(T& command, const BuildLikeParseState& state)
    -> util::Status {
    GUARD(finalize_profile_base(command, state));
    if (command.frozen) {
        command.locked = true;
        command.offline = true;
    }
    if (command.quiet && command.verbose > 0) {
        return std::unexpected(util::make_error(
            "the arguments '--quiet' and '--verbose' cannot be used together"));
    }
    return util::Ok;
}

auto finalize_command(cli::BuildCommand& command,
                      const BuildLikeParseState& state) -> util::Status {
    return finalize_build_like_base(command, state);
}

auto finalize_command(cli::CheckCommand& command,
                      const BuildLikeParseState& state) -> util::Status {
    return finalize_build_like_base(command, state);
}

auto finalize_command(cli::RunCommand& command,
                      const BuildLikeParseState& state) -> util::Status {
    GUARD(finalize_profile_base(command, state));
    if (command.bin.has_value() && command.example.has_value()) {
        return std::unexpected(util::make_error(
            "the arguments '--bin' and '--example' cannot be used together"));
    }
    return util::Ok;
}

auto finalize_command(cli::TestCommand& command,
                      const BuildLikeParseState& state) -> util::Status {
    GUARD(finalize_build_like_base(command, state));
    const bool has_other_selectors =
        command.tests_all || !command.tests.empty() || command.examples_all ||
        !command.examples.empty() || command.benches_all ||
        !command.benches.empty();
    if (command.scope == cli::TestScope::Unit && has_other_selectors) {
        return std::unexpected(util::make_error(
            "the '--unit' selector cannot be combined with other target "
            "selectors"));
    }
    return util::Ok;
}

template <typename CommandType>
auto parse_build_like_command(std::span<char*> args, const CommandSpec& spec,
                              const GlobalOptions& global_options)
    -> util::Result<cli::ParsedCommand> {
    validate_shared_option_contracts<CommandType>();

    CommandType command{};
    BuildLikeParseState state{};
    bool passthrough = false;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view value(args[i]);

        if (passthrough) {
            if constexpr (std::same_as<CommandType, cli::TestCommand>) {
                command.passthrough_args.push_back(std::string(value));
                continue;
            }
        }

        if (spec.allow_passthrough && value == "--"sv) {
            passthrough = true;
            continue;
        }

        if (is_quiet_token(value)) {
            continue;
        }

        if (const auto* option = find_matching_option(spec.options, value);
            option != nullptr) {
            if (option->action == OptionAction::Help) {
                return cli::ParsedCommand{
                    cli::HelpCommand{.topic = spec.help_topic}};
            }
            std::optional<std::string> parsed_value;
            const auto option_name = matched_option_name(*option, value);
            if (option->takes_value) {
                auto read =
                    read_option_value(args, i, option_name)
                        .transform_error([&](const auto& err) {
                            return make_usage_error_value(spec, err.message);
                        });
                if (!read) {
                    return std::unexpected(std::move(read.error()));
                }
                if (option->action == OptionAction::Profile &&
                    *read == "test" && spec.profile_test_is_unsupported) {
                    return make_unsupported_error(spec, "--profile test");
                }
                parsed_value = std::move(*read);
            }
            auto status =
                apply_shared_build_like_option(command, state, *option,
                                               parsed_value, option_name)
                    .transform_error([&](const auto& err) {
                        return make_usage_error_value(spec, err.message);
                    });
            if (!status) {
                return std::unexpected(std::move(status.error()));
            }
            continue;
        }

        if (is_unsupported_option(spec.unsupported_options, value)) {
            return make_unsupported_error(spec, value);
        }

        if (!value.empty() && value.front() == '-') {
            return make_usage_error(
                spec, std::format("unexpected option '{}'", value));
        }

        auto positional = handle_positional_argument(command, value);
        if (!positional) {
            return make_usage_error(spec, positional.error().message);
        }
    }

    apply_global_options(command, global_options);

    auto status =
        finalize_command(command, state).transform_error([&](const auto& err) {
            return make_usage_error_value(spec, err.message);
        });
    if (!status) {
        return std::unexpected(std::move(status.error()));
    }
    return cli::ParsedCommand{std::move(command)};
}

auto parse_version_command(std::span<char*> args, bool implicit_verbose = false)
    -> util::Result<cli::ParsedCommand> {
    cli::VersionCommand command{};
    command.verbose = implicit_verbose;

    for (const auto value : non_quiet_tokens(args)) {
        if (value == "-v"sv || value == "--verbose"sv || value == "-Vv"sv) {
            command.verbose = true;
            continue;
        }
        return std::unexpected(make_usage_error_value(
            version_usage(), unexpected_argument_error(value).message));
    }

    return cli::ParsedCommand{command};
}

auto parse_help_command(std::span<char*> args)
    -> util::Result<cli::ParsedCommand> {
    auto tokens = non_quiet_tokens(args);
    auto it = tokens.begin();
    if (it == tokens.end()) {
        return cli::ParsedCommand{
            cli::HelpCommand{.topic = cli::HelpTopic::Root}};
    }

    const std::string_view topic_arg(*it++);
    if (topic_arg == "-h"sv || topic_arg == "--help"sv) {
        return cli::ParsedCommand{
            cli::HelpCommand{.topic = cli::HelpTopic::Help}};
    }

    auto topic = cli::help_topic_from_name(topic_arg);
    if (!topic.has_value()) {
        return util::make_unexpected(format_unknown_command(topic_arg));
    }

    if (it != tokens.end()) {
        return std::unexpected(make_usage_error_value(
            help_usage(), unexpected_argument_error(*it).message));
    }

    return cli::ParsedCommand{cli::HelpCommand{.topic = *topic}};
}

}  // namespace

namespace cli {

auto parse(std::span<char*> args) -> util::Result<ParsedCommand> {
    if (args.size() < 2) {
        return HelpCommand{.topic = HelpTopic::Root};
    }

    auto global_result = parse_global_options(args);
    if (!global_result) {
        return std::unexpected(std::move(global_result.error()));
    }

    const auto& global = global_result->options;

    if (global.version) {
        return VersionCommand{.verbose = global.verbose_version};
    }
    if (global.help) {
        return HelpCommand{.topic = HelpTopic::Root};
    }
    if (global_result->command_index >= args.size()) {
        return HelpCommand{.topic = HelpTopic::Root};
    }
    const auto command_args = args.subspan(global_result->command_index);
    const std::string_view cmd(command_args[0]);

    const auto make_command = [&](auto command) -> util::Result<ParsedCommand> {
        return apply_global_options(ParsedCommand{std::move(command)}, global);
    };
    const auto apply_result =
        [&](util::Result<ParsedCommand> result) -> util::Result<ParsedCommand> {
        if (!result) {
            return std::unexpected(std::move(result.error()));
        }
        return apply_global_options(std::move(result.value()), global);
    };

    if (cmd == "init"sv) {
        if (has_help_flag(command_args)) {
            return make_command(HelpCommand{.topic = HelpTopic::Init});
        }
        return make_command(InitCommand{});
    }

    if (cmd == "new"sv) {
        if (has_help_flag(command_args)) {
            return make_command(HelpCommand{.topic = HelpTopic::New});
        }
        const auto path = first_non_quiet_token(command_args);
        if (!path.has_value()) {
            return util::make_unexpected(
                format_missing_required_args("<PATH>", "argo new <PATH>"));
        }
        return make_command(NewCommand{.name = std::string(*path)});
    }

    if (cmd == "add"sv) {
        if (has_help_flag(command_args)) {
            return make_command(HelpCommand{.topic = HelpTopic::Add});
        }
        const auto dependency = first_non_quiet_token(command_args);
        if (!dependency.has_value()) {
            return util::make_unexpected(format_missing_required_args(
                "<DEP_SPEC>", "argo add <DEP_SPEC>"));
        }
        return make_command(AddCommand{.package = std::string(*dependency)});
    }

    if (cmd == "remove"sv) {
        if (has_help_flag(command_args)) {
            return make_command(HelpCommand{.topic = HelpTopic::Remove});
        }
        const auto dependency = first_non_quiet_token(command_args);
        if (!dependency.has_value()) {
            return util::make_unexpected(
                format_missing_required_args("<DEP>", "argo remove <DEP>"));
        }
        return make_command(RemoveCommand{.package = std::string(*dependency)});
    }

    if (cmd == "update"sv) {
        if (has_help_flag(command_args)) {
            return make_command(HelpCommand{.topic = HelpTopic::Update});
        }
        if (const auto dependency = first_non_quiet_token(command_args);
            dependency.has_value()) {
            return make_command(
                UpdateCommand{.package = std::string(*dependency)});
        }
        return make_command(UpdateCommand{});
    }

    if (cmd == "fetch"sv) {
        if (has_help_flag(command_args)) {
            return make_command(HelpCommand{.topic = HelpTopic::Fetch});
        }
        return make_command(FetchCommand{});
    }

    if (cmd == "publish"sv) {
        if (has_help_flag(command_args)) {
            return make_command(HelpCommand{.topic = HelpTopic::Publish});
        }
        return make_command(PublishCommand{});
    }

    if (cmd == "yank"sv) {
        if (has_help_flag(command_args)) {
            return make_command(HelpCommand{.topic = HelpTopic::Yank});
        }
        auto version = require_option_value(
            command_args, "--vers", "argo yank --vers <VERSION> [--undo]");
        if (!version) {
            return std::unexpected(std::move(version.error()));
        }
        return make_command(YankCommand{
            .version = *version, .undo = has_flag(command_args, "--undo"sv)});
    }

    if (cmd == "owner"sv) {
        if (has_help_flag(command_args)) {
            return make_command(HelpCommand{.topic = HelpTopic::Owner});
        }
        OwnerCommand owner;
        owner.add_owner = find_option_value(command_args, "--add");
        owner.remove_owner = find_option_value(command_args, "--remove");
        if (owner.add_owner.has_value() == owner.remove_owner.has_value()) {
            return util::make_unexpected(
                std::format("Usage Error: owner requires exactly one of --add "
                            "<OWNER> or --remove <OWNER>.\n\nUsage: {}",
                            "argo owner --add <OWNER>\n       argo owner "
                            "--remove <OWNER>"));
        }
        return make_command(std::move(owner));
    }

    if (cmd == "login"sv) {
        if (has_help_flag(command_args)) {
            return make_command(HelpCommand{.topic = HelpTopic::Login});
        }
        LoginCommand login;
        login.registry =
            find_option_value(command_args, "--registry").value_or("");
        login.token = find_option_value(command_args, "--token").value_or("");
        if (login.token.empty()) {
            return util::make_unexpected(std::format(
                "Usage Error: login requires --token <TOKEN>.\n\nUsage: {}",
                "argo login --token <TOKEN> [--registry <NAME>]"));
        }
        return make_command(std::move(login));
    }

    if (cmd == "logout"sv) {
        if (has_help_flag(command_args)) {
            return make_command(HelpCommand{.topic = HelpTopic::Logout});
        }
        return make_command(LogoutCommand{
            .registry =
                find_option_value(command_args, "--registry").value_or("")});
    }

    if (cmd == "vcpkg"sv) {
        if (has_help_flag(command_args)) {
            return make_command(HelpCommand{.topic = HelpTopic::Vcpkg});
        }
        return make_command(VcpkgCommand{});
    }

    if (cmd == "ppargo"sv) {
        if (has_help_flag(command_args)) {
            return make_command(HelpCommand{.topic = HelpTopic::Ppargo});
        }
        return make_command(PpargoCommand{});
    }

    if (cmd == "build"sv || cmd == "b"sv) {
        return parse_build_like_command<cli::BuildCommand>(
            command_args, kBuildCommandSpec, global);
    }

    if (cmd == "check"sv || cmd == "c"sv) {
        return parse_build_like_command<cli::CheckCommand>(
            command_args, kCheckCommandSpec, global);
    }

    if (cmd == "run"sv || cmd == "r"sv) {
        return parse_build_like_command<cli::RunCommand>(
            command_args, kRunCommandSpec, global);
    }

    if (cmd == "test"sv || cmd == "t"sv) {
        return parse_build_like_command<cli::TestCommand>(
            command_args, kTestCommandSpec, global);
    }

    if (cmd == "version"sv) {
        if (has_help_flag(command_args)) {
            return make_command(HelpCommand{.topic = HelpTopic::Version});
        }
        return apply_result(parse_version_command(command_args));
    }

    if (cmd == "help"sv) {
        return apply_result(parse_help_command(command_args));
    }

    return util::make_unexpected(format_unknown_command(cmd));
}

}  // namespace cli
