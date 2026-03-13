#include "cli/help.hpp"

#include <string_view>

using namespace std::string_view_literals;

namespace cli {

namespace {

constexpr std::string_view kRootHelpText =
    "C++'s build system and package manager\n\n"
    "Usage: argo [OPTIONS] [COMMAND]\n\n"
    "Options:\n"
    "  -V, --version                  Print version info and exit\n"
    "  -q, --quiet                    Do not print cargo log messages\n"
    "  -h, --help                     Print help\n\n"
    "Commands:\n"
    "  new             Create a new ppargo package\n"
    "  init            Create a new ppargo package in an existing directory\n"
    "  build, b        Compile the current package\n"
    "  check, c        Analyze the current package and report errors\n"
    "  run, r          Run a binary or example of the local package\n"
    "  test, t         Run the tests\n"
    "  vcpkg           Set package_manager to vcpkg\n"
    "  ppargo          Set package_manager to ppargo\n"
    "  add             Add dependencies to the manifest\n"
    "  remove          Remove dependencies from the manifest\n"
    "  update          Update dependencies listed in ppargo.lock\n"
    "  fetch           Fetch dependency sources\n"
    "  publish         Publish the current package\n"
    "  yank            Yank or unyank a published version\n"
    "  owner           Manage package owners\n"
    "  login           Store registry token\n"
    "  logout          Remove registry token\n"
    "  version         Print version information\n"
    "  help            Display help for ppargo or a command\n";

constexpr std::string_view kHelpHelpText =
    "Display help for ppargo or a command.\n\n"
    "Usage: argo help [COMMAND]\n\n"
    "Arguments:\n"
    "  [COMMAND]                Command to describe\n\n"
    "Options:\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kNewHelpText =
    "Create a new package at <path>.\n\n"
    "Usage: argo new <PATH>\n\n"
    "Arguments:\n"
    "  <PATH>                  Directory to create for the new package\n\n"
    "Options:\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kInitHelpText =
    "Create a new package in the current directory.\n\n"
    "Usage: argo init\n\n"
    "Options:\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kAddHelpText =
    "Add a dependency specification to the current package.\n\n"
    "Usage: argo add <DEP_SPEC>\n\n"
    "Arguments:\n"
    "  <DEP_SPEC>              Dependency spec such as zlib@1.3.1\n\n"
    "Options:\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kRemoveHelpText =
    "Remove a dependency from the current package.\n\n"
    "Usage: argo remove <DEP>\n\n"
    "Arguments:\n"
    "  <DEP>                   Dependency name to remove\n\n"
    "Options:\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kUpdateHelpText =
    "Update the current lockfile resolution.\n\n"
    "Usage: argo update [DEP]\n\n"
    "Arguments:\n"
    "  [DEP]                   Optional dependency name to update\n\n"
    "Options:\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kFetchHelpText =
    "Fetch dependency sources for the current package.\n\n"
    "Usage: argo fetch\n\n"
    "Options:\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kPublishHelpText =
    "Publish the current package through the configured backend.\n\n"
    "Usage: argo publish\n\n"
    "Options:\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kYankHelpText =
    "Yank or unyank a published version.\n\n"
    "Usage: argo yank --vers <VERSION> [--undo]\n\n"
    "Options:\n"
    "  --vers <VERSION>        Version to yank or unyank\n"
    "  --undo                  Undo a previous yank\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kOwnerHelpText =
    "Manage package owners.\n\n"
    "Usage: argo owner --add <OWNER>\n"
    "       argo owner --remove <OWNER>\n\n"
    "Options:\n"
    "  --add <OWNER>           Add an owner\n"
    "  --remove <OWNER>        Remove an owner\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kLoginHelpText =
    "Store a registry token.\n\n"
    "Usage: argo login --token <TOKEN> [--registry <NAME>]\n\n"
    "Options:\n"
    "  --token <TOKEN>         Token to store\n"
    "  --registry <NAME>       Registry name to target\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kLogoutHelpText =
    "Remove a stored registry token.\n\n"
    "Usage: argo logout [--registry <NAME>]\n\n"
    "Options:\n"
    "  --registry <NAME>       Registry name to clear\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kVcpkgHelpText =
    "Set the package manager feature to vcpkg.\n\n"
    "Usage: argo vcpkg\n\n"
    "Options:\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kPpargoHelpText =
    "Set the package manager feature to ppargo.\n\n"
    "Usage: argo ppargo\n\n"
    "Options:\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kBuildHelpText =
    "Compile local packages and all of their dependencies.\n\n"
    "Usage: argo build [options]\n\n"
    "Target Selection:\n"
    "  --bin <name>            Build the specified binary target (repeatable)\n"
    "  --bins                  Build all binary targets\n"
    "  --example <name>        Build the specified example target "
    "(repeatable)\n"
    "  --examples              Build all example targets\n"
    "  --test <name>           Build the specified integration test target "
    "(repeatable)\n"
    "  --tests                 Build unit and integration test targets\n"
    "  --all-targets           Build bins, examples, and tests\n\n"
    "Compilation Options:\n"
    "  -r, --release           Build with the release profile\n"
    "  --profile <dev|release> Select the build profile\n"
    "  -j, --jobs <N>          Override parallel job count for this "
    "invocation\n"
    "  --target-dir <dir>      Override the artifact output directory\n\n"
    "Dependency Workflow:\n"
    "  --locked                Require an existing up-to-date ppargo.lock\n"
    "  --offline               Use only already available local dependency "
    "state\n"
    "  --frozen                Equivalent to --locked --offline\n\n"
    "Display Options:\n"
    "  -v, -vv, --verbose      Increase build output detail\n"
    "  -q, --quiet             Suppress normal build output\n"
    "  --color <when>          Control color output (auto, always, never)\n"
    "  --manifest-path <path>  Use the specified ppargo.toml\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kCheckHelpText =
    "Check a local package and all of its dependencies for errors.\n\n"
    "Usage: argo check [options]\n\n"
    "Target Selection:\n"
    "  --bin <name>            Check the specified binary target (repeatable)\n"
    "  --bins                  Check all binary targets\n"
    "  --example <name>        Check the specified example target "
    "(repeatable)\n"
    "  --examples              Check all example targets\n"
    "  --test <name>           Check the specified integration test target "
    "(repeatable)\n"
    "  --tests                 Check unit and integration test targets\n"
    "  --bench <name>          Check the specified bench target (repeatable)\n"
    "  --benches               Check all bench targets\n"
    "  --all-targets           Check bins, examples, tests, and benches\n\n"
    "Compilation Options:\n"
    "  -r, --release           Check with the release profile\n"
    "  --profile <dev|release> Select the check profile\n"
    "  -j, --jobs <N>          Override parallel job count for this "
    "invocation\n"
    "  --target-dir <dir>      Override the artifact output directory\n"
    "  --keep-going            Continue to later selected targets after "
    "failures\n\n"
    "Dependency Workflow:\n"
    "  --locked                Require an existing up-to-date ppargo.lock\n"
    "  --offline               Use only already available local dependency "
    "state\n"
    "  --frozen                Equivalent to --locked --offline\n\n"
    "Display Options:\n"
    "  -v, -vv, --verbose      Increase check output detail\n"
    "  -q, --quiet             Suppress normal check output\n"
    "  --color <when>          Control color output (auto, always, never)\n"
    "  --manifest-path <path>  Use the specified ppargo.toml\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kRunHelpText =
    "Build and run a local binary or example target.\n\n"
    "Usage: argo run [--release] [--bin <name> | --example <name>]\n\n"
    "Target Selection:\n"
    "  --bin <name>            Run the specified binary target\n"
    "  --example <name>        Run the specified example target\n\n"
    "Compilation Options:\n"
    "  -r, --release           Build with the release profile\n\n"
    "Display Options:\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kTestHelpText =
    "Compile and execute unit and integration tests of a package.\n\n"
    "Usage: argo test [options] [testname] [-- test-options]\n\n"
    "Default behavior:\n"
    "  Without selectors, argo test runs unit tests first.\n"
    "  If no unit tests exist, it falls back to integration tests.\n\n"
    "Test Options:\n"
    "  --no-run                Build selected targets but do not run them\n"
    "  --no-fail-fast          Continue running later executables after "
    "failures\n\n"
    "Target Selection:\n"
    "  --unit                  Select only the unit-test aggregate\n"
    "  --integration           Select all integration test executables\n"
    "  --test <name>           Select matching integration tests (repeatable)\n"
    "  --tests                 Select unit and integration tests\n"
    "  --example <name>        Build and run matching examples (repeatable)\n"
    "  --examples              Build and run all examples\n"
    "  --bench <name>          Build and run matching benches (repeatable)\n"
    "  --benches               Build and run all benches\n"
    "  --all-targets           Select examples, tests, and benches\n\n"
    "Compilation Options:\n"
    "  -r, --release           Build with the release profile\n"
    "  --profile <dev|release> Select the build profile\n"
    "  -j, --jobs <N>          Override parallel job count for this "
    "invocation\n"
    "  --target-dir <dir>      Override the artifact output directory\n\n"
    "Dependency Workflow:\n"
    "  --locked                Require an existing up-to-date ppargo.lock\n"
    "  --offline               Use only already available local dependency "
    "state\n"
    "  --frozen                Equivalent to --locked --offline\n\n"
    "Display Options:\n"
    "  -v, -vv, --verbose      Increase test output detail\n"
    "  -q, --quiet             Suppress normal test output\n"
    "  --color <when>          Control color output (auto, always, never)\n"
    "  --manifest-path <path>  Use the specified ppargo.toml\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kVersionHelpText =
    "Print version information.\n\n"
    "Usage: argo version [--verbose]\n\n"
    "Options:\n"
    "  -v, --verbose           Show detailed version information\n"
    "  -h, --help              Print this help\n";

}  // namespace

auto help_text(HelpTopic topic) -> std::string_view {
    switch (topic) {
        case HelpTopic::Root:
            return kRootHelpText;
        case HelpTopic::Help:
            return kHelpHelpText;
        case HelpTopic::New:
            return kNewHelpText;
        case HelpTopic::Init:
            return kInitHelpText;
        case HelpTopic::Add:
            return kAddHelpText;
        case HelpTopic::Remove:
            return kRemoveHelpText;
        case HelpTopic::Update:
            return kUpdateHelpText;
        case HelpTopic::Fetch:
            return kFetchHelpText;
        case HelpTopic::Publish:
            return kPublishHelpText;
        case HelpTopic::Yank:
            return kYankHelpText;
        case HelpTopic::Owner:
            return kOwnerHelpText;
        case HelpTopic::Login:
            return kLoginHelpText;
        case HelpTopic::Logout:
            return kLogoutHelpText;
        case HelpTopic::Vcpkg:
            return kVcpkgHelpText;
        case HelpTopic::Ppargo:
            return kPpargoHelpText;
        case HelpTopic::Build:
            return kBuildHelpText;
        case HelpTopic::Check:
            return kCheckHelpText;
        case HelpTopic::Run:
            return kRunHelpText;
        case HelpTopic::Test:
            return kTestHelpText;
        case HelpTopic::Version:
            return kVersionHelpText;
    }
    return {};
}

auto help_topic_from_name(std::string_view name) -> std::optional<HelpTopic> {
    if (name == "help"sv) {
        return HelpTopic::Help;
    }
    if (name == "new"sv) {
        return HelpTopic::New;
    }
    if (name == "init"sv) {
        return HelpTopic::Init;
    }
    if (name == "add"sv) {
        return HelpTopic::Add;
    }
    if (name == "remove"sv) {
        return HelpTopic::Remove;
    }
    if (name == "update"sv) {
        return HelpTopic::Update;
    }
    if (name == "fetch"sv) {
        return HelpTopic::Fetch;
    }
    if (name == "publish"sv) {
        return HelpTopic::Publish;
    }
    if (name == "yank"sv) {
        return HelpTopic::Yank;
    }
    if (name == "owner"sv) {
        return HelpTopic::Owner;
    }
    if (name == "login"sv) {
        return HelpTopic::Login;
    }
    if (name == "logout"sv) {
        return HelpTopic::Logout;
    }
    if (name == "vcpkg"sv) {
        return HelpTopic::Vcpkg;
    }
    if (name == "ppargo"sv) {
        return HelpTopic::Ppargo;
    }
    if (name == "build"sv || name == "b"sv) {
        return HelpTopic::Build;
    }
    if (name == "check"sv || name == "c"sv) {
        return HelpTopic::Check;
    }
    if (name == "run"sv || name == "r"sv) {
        return HelpTopic::Run;
    }
    if (name == "test"sv || name == "t"sv) {
        return HelpTopic::Test;
    }
    if (name == "version"sv) {
        return HelpTopic::Version;
    }
    return std::nullopt;
}

}  // namespace cli
