#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "cli/help.hpp"

namespace cli::catalog {

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

struct BuildLikeCommandSpec {
    std::string_view command_name;
    std::string_view usage_line;
    HelpTopic help_topic;
    std::span<const OptionSpec> options;
    std::span<const UnsupportedOptionSpec> unsupported_options;
    bool allow_passthrough = false;
    bool profile_test_is_unsupported = false;
};

struct CommandMetadata {
    HelpTopic topic;
    std::string_view canonical_name;
    std::span<const std::string_view> aliases;
    std::string_view root_label;
    std::string_view summary;
    std::string_view usage_line;
    std::string_view help_text;
    const BuildLikeCommandSpec* build_like = nullptr;
    bool supports_color_option = false;
};

auto metadata() -> std::span<const CommandMetadata>;
auto command_name(HelpTopic topic) -> std::string_view;
auto root_label(HelpTopic topic) -> std::string_view;
auto summary(HelpTopic topic) -> std::string_view;
auto usage_line(HelpTopic topic) -> std::string_view;
auto help_text(HelpTopic topic) -> std::string_view;
auto matches(std::string_view name, HelpTopic topic) -> bool;
auto help_topic_from_name(std::string_view name) -> std::optional<HelpTopic>;
auto build_like_spec(HelpTopic topic) -> const BuildLikeCommandSpec*;
auto command_supports_color_option(std::string_view name) -> bool;

}  // namespace cli::catalog
