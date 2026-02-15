#pragma once

#include <string>
#include <variant>

#include "util/result.hpp"

namespace cli {

struct NewCommand {
    std::string name;
    auto execute() const -> util::Status;
};

struct AddCommand {
    std::string package;
    auto execute() const -> util::Status;
};

struct BuildCommand {
    bool release = false;
    auto execute() const -> util::Status;
};

struct CheckCommand {
    bool release = false;
    auto execute() const -> util::Status;
};

struct RunCommand {
    bool release = false;
    auto execute() const -> util::Status;
};

struct VersionCommand {
    auto execute() const -> util::Status;
};
struct HelpCommand {
    auto execute() const -> util::Status;
};
struct InitCommand {
    auto execute() const -> util::Status;
};

using ParsedCommand =
    std::variant<NewCommand, AddCommand, BuildCommand, CheckCommand, RunCommand,
                 VersionCommand, HelpCommand, InitCommand>;

}  // namespace cli
