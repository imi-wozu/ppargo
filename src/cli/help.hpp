#pragma once

#include <optional>
#include <string_view>

namespace cli {

enum class HelpTopic {
    Root,
    Help,
    New,
    Init,
    Add,
    Remove,
    Update,
    Fetch,
    Publish,
    Yank,
    Owner,
    Login,
    Logout,
    Vcpkg,
    Ppargo,
    Build,
    Check,
    Run,
    Test,
    Version,
};

auto help_text(HelpTopic topic) -> std::string_view;
auto help_topic_from_name(std::string_view name) -> std::optional<HelpTopic>;

}  // namespace cli
