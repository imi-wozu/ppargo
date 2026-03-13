#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "cli/help.hpp"
#include "util/result.hpp"

namespace cli {

enum class TestScope {
    All,
    Unit,
    Integration,
};

struct NewCommand {
    std::string name;
    auto execute() const -> util::Status;
};

struct AddCommand {
    std::string package;
    auto execute() const -> util::Status;
};

struct RemoveCommand {
    std::string package;
    auto execute() const -> util::Status;
};

struct UpdateCommand {
    std::optional<std::string> package;
    auto execute() const -> util::Status;
};

struct FetchCommand {
    auto execute() const -> util::Status;
};

struct PublishCommand {
    auto execute() const -> util::Status;
};

struct YankCommand {
    std::string version;
    bool undo = false;
    auto execute() const -> util::Status;
};

struct OwnerCommand {
    std::optional<std::string> add_owner;
    std::optional<std::string> remove_owner;
    auto execute() const -> util::Status;
};

struct LoginCommand {
    std::string registry;
    std::string token;
    auto execute() const -> util::Status;
};

struct LogoutCommand {
    std::string registry;
    auto execute() const -> util::Status;
};

struct VcpkgCommand {
    auto execute() const -> util::Status;
};

struct PpargoCommand {
    auto execute() const -> util::Status;
};

struct BuildCommand {
    bool release = false;
    std::vector<std::string> bins;
    bool bins_all = false;
    std::vector<std::string> examples;
    bool examples_all = false;
    std::vector<std::string> tests;
    bool tests_all = false;
    std::optional<std::string> manifest_path;
    std::optional<std::string> target_dir;
    std::optional<int> jobs;
    bool locked = false;
    bool offline = false;
    bool frozen = false;
    int verbose = 0;
    bool quiet = false;
    std::string color = "auto";
    auto execute() const -> util::Status;
};

struct CheckCommand {
    bool release = false;
    std::vector<std::string> bins;
    bool bins_all = false;
    std::vector<std::string> examples;
    bool examples_all = false;
    std::vector<std::string> tests;
    bool tests_all = false;
    std::vector<std::string> benches;
    bool benches_all = false;
    std::optional<std::string> manifest_path;
    std::optional<std::string> target_dir;
    std::optional<int> jobs;
    bool locked = false;
    bool offline = false;
    bool frozen = false;
    int verbose = 0;
    bool quiet = false;
    std::string color = "auto";
    bool keep_going = false;
    auto execute() const -> util::Status;
};

struct RunCommand {
    bool release = false;
    std::optional<std::string> bin;
    std::optional<std::string> example;
    bool quiet = false;
    auto execute() const -> util::Status;
};

struct TestCommand {
    bool release = false;
    TestScope scope = TestScope::All;
    std::vector<std::string> tests;
    bool tests_all = false;
    std::vector<std::string> examples;
    bool examples_all = false;
    std::vector<std::string> benches;
    bool benches_all = false;
    std::optional<std::string> manifest_path;
    std::optional<std::string> target_dir;
    std::optional<int> jobs;
    bool locked = false;
    bool offline = false;
    bool frozen = false;
    bool no_run = false;
    bool no_fail_fast = false;
    std::optional<std::string> test_filter;
    std::vector<std::string> passthrough_args;
    int verbose = 0;
    bool quiet = false;
    std::string color = "auto";
    auto execute() const -> util::Status;
};

struct VersionCommand {
    bool verbose = false;
    auto execute() const -> util::Status;
};
struct HelpCommand {
    HelpTopic topic = HelpTopic::Root;
    auto execute() const -> util::Status;
};
struct InitCommand {
    auto execute() const -> util::Status;
};

using ParsedCommand =
    std::variant<NewCommand, AddCommand, RemoveCommand, UpdateCommand,
                 FetchCommand, PublishCommand, YankCommand, OwnerCommand,
                 LoginCommand, LogoutCommand, VcpkgCommand, PpargoCommand,
                 BuildCommand, CheckCommand, RunCommand, VersionCommand,
                 HelpCommand, TestCommand, InitCommand>;

}  // namespace cli
