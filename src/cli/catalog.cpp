#include "cli/catalog.hpp"

#include <algorithm>
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace cli::catalog {

namespace {

constexpr std::string_view kHelpHelpText =
    "Display help for ppargo or a command.\n\n"
    "Usage: argo help [COMMAND]\n\n"
    "Arguments:\n"
    "  [COMMAND]                Command to describe\n\n"
    "Options:\n"
    "  -q, --quiet             Do not print cargo log messages\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kNewHelpText =
    "Create a new package at <path>.\n\n"
    "Usage: argo new <PATH>\n\n"
    "Arguments:\n"
    "  <PATH>                  Directory to create for the new package\n\n"
    "Options:\n"
    "  -q, --quiet             Do not print cargo log messages\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kInitHelpText =
    "Create a new package in the current directory.\n\n"
    "Usage: argo init\n\n"
    "Options:\n"
    "  -q, --quiet             Do not print cargo log messages\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kAddHelpText =
    "Add a dependency specification to the current package.\n\n"
    "Usage: argo add <DEP_SPEC>\n\n"
    "Arguments:\n"
    "  <DEP_SPEC>              Dependency spec such as zlib@1.3.1\n\n"
    "Options:\n"
    "  -q, --quiet             Do not print cargo log messages\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kRemoveHelpText =
    "Remove a dependency from the current package.\n\n"
    "Usage: argo remove <DEP>\n\n"
    "Arguments:\n"
    "  <DEP>                   Dependency name to remove\n\n"
    "Options:\n"
    "  -q, --quiet             Do not print cargo log messages\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kUpdateHelpText =
    "Update the current lockfile resolution.\n\n"
    "Usage: argo update [DEP]\n\n"
    "Arguments:\n"
    "  [DEP]                   Optional dependency name to update\n\n"
    "Options:\n"
    "  -q, --quiet             Do not print cargo log messages\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kFetchHelpText =
    "Fetch dependency sources for the current package.\n\n"
    "Usage: argo fetch\n\n"
    "Options:\n"
    "  -q, --quiet             Do not print cargo log messages\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kPublishHelpText =
    "Publish the current package through the configured backend.\n\n"
    "Usage: argo publish\n\n"
    "Options:\n"
    "  -q, --quiet             Do not print cargo log messages\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kYankHelpText =
    "Yank or unyank a published version.\n\n"
    "Usage: argo yank --vers <VERSION> [--undo]\n\n"
    "Options:\n"
    "  --vers <VERSION>        Version to yank or unyank\n"
    "  --undo                  Undo a previous yank\n"
    "  -q, --quiet             Do not print cargo log messages\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kOwnerHelpText =
    "Manage package owners.\n\n"
    "Usage: argo owner --add <OWNER>\n"
    "       argo owner --remove <OWNER>\n\n"
    "Options:\n"
    "  --add <OWNER>           Add an owner\n"
    "  --remove <OWNER>        Remove an owner\n"
    "  -q, --quiet             Do not print cargo log messages\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kLoginHelpText =
    "Store a registry token.\n\n"
    "Usage: argo login --token <TOKEN> [--registry <NAME>]\n\n"
    "Options:\n"
    "  --token <TOKEN>         Token to store\n"
    "  --registry <NAME>       Registry name to target\n"
    "  -q, --quiet             Do not print cargo log messages\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kLogoutHelpText =
    "Remove a stored registry token.\n\n"
    "Usage: argo logout [--registry <NAME>]\n\n"
    "Options:\n"
    "  --registry <NAME>       Registry name to clear\n"
    "  -q, --quiet             Do not print cargo log messages\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kVcpkgHelpText =
    "Set the package manager feature to vcpkg.\n\n"
    "Usage: argo vcpkg\n\n"
    "Options:\n"
    "  -q, --quiet             Do not print cargo log messages\n"
    "  -h, --help              Print this help\n";

constexpr std::string_view kPpargoHelpText =
    "Set the package manager feature to ppargo.\n\n"
    "Usage: argo ppargo\n\n"
    "Options:\n"
    "  -q, --quiet             Do not print cargo log messages\n"
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
    "  -q, --quiet             Do not print cargo log messages\n"
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
    "  -q, --quiet             Do not print cargo log messages\n"
    "  -h, --help              Print this help\n";

constexpr std::array kBuildAliases{"b"sv};
constexpr std::array kCheckAliases{"c"sv};
constexpr std::array kRunAliases{"r"sv};
constexpr std::array kTestAliases{"t"sv};

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

constexpr BuildLikeCommandSpec kBuildCommandSpec{
    "build",
    "argo build [options]",
    HelpTopic::Build,
    std::span{kBuildOptions},
    std::span{kBuildUnsupportedOptions},
    false,
    false};

constexpr BuildLikeCommandSpec kCheckCommandSpec{
    "check",
    "argo check [options]",
    HelpTopic::Check,
    std::span{kCheckOptions},
    std::span{kCheckUnsupportedOptions},
    false,
    true};

constexpr BuildLikeCommandSpec kRunCommandSpec{
    "run",
    "argo run [--release] [--bin <NAME> | --example <NAME>]",
    HelpTopic::Run,
    std::span{kRunOptions},
    std::span{kRunUnsupportedOptions},
    false,
    false};

constexpr BuildLikeCommandSpec kTestCommandSpec{
    "test",
    "argo test [options] [testname] [-- test-options]",
    HelpTopic::Test,
    std::span{kTestOptions},
    std::span{kTestUnsupportedOptions},
    true,
    false};

constexpr std::array kMetadata{
    CommandMetadata{HelpTopic::New,
                    "new"sv,
                    {},
                    "new"sv,
                    "Create a new ppargo package",
                    "argo new <PATH>",
                    kNewHelpText},
    CommandMetadata{HelpTopic::Init,
                    "init"sv,
                    {},
                    "init"sv,
                    "Create a new ppargo package in an existing directory",
                    "argo init",
                    kInitHelpText},
    CommandMetadata{HelpTopic::Build, "build"sv, kBuildAliases, "build, b"sv,
                    "Compile the current package", kBuildCommandSpec.usage_line,
                    kBuildHelpText, &kBuildCommandSpec, true},
    CommandMetadata{HelpTopic::Check, "check"sv, kCheckAliases, "check, c"sv,
                    "Analyze the current package and report errors",
                    kCheckCommandSpec.usage_line, kCheckHelpText,
                    &kCheckCommandSpec, true},
    CommandMetadata{HelpTopic::Run, "run"sv, kRunAliases, "run, r"sv,
                    "Run a binary or example of the local package",
                    kRunCommandSpec.usage_line, kRunHelpText, &kRunCommandSpec},
    CommandMetadata{HelpTopic::Test, "test"sv, kTestAliases, "test, t"sv,
                    "Run the tests", kTestCommandSpec.usage_line, kTestHelpText,
                    &kTestCommandSpec, true},
    CommandMetadata{HelpTopic::Vcpkg,
                    "vcpkg"sv,
                    {},
                    "vcpkg"sv,
                    "Set package_manager to vcpkg",
                    "argo vcpkg",
                    kVcpkgHelpText},
    CommandMetadata{HelpTopic::Ppargo,
                    "ppargo"sv,
                    {},
                    "ppargo"sv,
                    "Set package_manager to ppargo",
                    "argo ppargo",
                    kPpargoHelpText},
    CommandMetadata{HelpTopic::Add,
                    "add"sv,
                    {},
                    "add"sv,
                    "Add dependencies to the manifest",
                    "argo add <DEP_SPEC>",
                    kAddHelpText},
    CommandMetadata{HelpTopic::Remove,
                    "remove"sv,
                    {},
                    "remove"sv,
                    "Remove dependencies from the manifest",
                    "argo remove <DEP>",
                    kRemoveHelpText},
    CommandMetadata{HelpTopic::Update,
                    "update"sv,
                    {},
                    "update"sv,
                    "Update dependencies listed in ppargo.lock",
                    "argo update [DEP]",
                    kUpdateHelpText},
    CommandMetadata{HelpTopic::Fetch,
                    "fetch"sv,
                    {},
                    "fetch"sv,
                    "Fetch dependency sources",
                    "argo fetch",
                    kFetchHelpText},
    CommandMetadata{HelpTopic::Publish,
                    "publish"sv,
                    {},
                    "publish"sv,
                    "Publish the current package",
                    "argo publish",
                    kPublishHelpText},
    CommandMetadata{HelpTopic::Yank,
                    "yank"sv,
                    {},
                    "yank"sv,
                    "Yank or unyank a published version",
                    "argo yank --vers <VERSION> [--undo]",
                    kYankHelpText},
    CommandMetadata{HelpTopic::Owner,
                    "owner"sv,
                    {},
                    "owner"sv,
                    "Manage package owners",
                    "argo owner --add <OWNER>",
                    kOwnerHelpText},
    CommandMetadata{HelpTopic::Login,
                    "login"sv,
                    {},
                    "login"sv,
                    "Store registry token",
                    "argo login --token <TOKEN> [--registry <NAME>]",
                    kLoginHelpText},
    CommandMetadata{HelpTopic::Logout,
                    "logout"sv,
                    {},
                    "logout"sv,
                    "Remove registry token",
                    "argo logout [--registry <NAME>]",
                    kLogoutHelpText},
    CommandMetadata{HelpTopic::Version,
                    "version"sv,
                    {},
                    "version"sv,
                    "Print version information",
                    "argo version [--verbose]",
                    kVersionHelpText},
    CommandMetadata{HelpTopic::Help,
                    "help"sv,
                    {},
                    "help"sv,
                    "Display help for ppargo or a command",
                    "argo help [COMMAND]",
                    kHelpHelpText},
};

auto find_by_topic(HelpTopic topic) -> const CommandMetadata* {
    const auto found = std::ranges::find_if(
        kMetadata,
        [&](const CommandMetadata& entry) { return entry.topic == topic; });
    return found == kMetadata.end() ? nullptr : &*found;
}

auto find_by_name(std::string_view name) -> const CommandMetadata* {
    const auto found =
        std::ranges::find_if(kMetadata, [&](const CommandMetadata& entry) {
            if (entry.canonical_name == name) {
                return true;
            }
            return std::ranges::any_of(
                entry.aliases,
                [&](std::string_view alias) { return alias == name; });
        });
    return found == kMetadata.end() ? nullptr : &*found;
}

}  // namespace

auto metadata() -> std::span<const CommandMetadata> { return kMetadata; }

auto command_name(HelpTopic topic) -> std::string_view {
    if (const auto* entry = find_by_topic(topic); entry != nullptr) {
        return entry->canonical_name;
    }
    return {};
}

auto root_label(HelpTopic topic) -> std::string_view {
    if (const auto* entry = find_by_topic(topic); entry != nullptr) {
        return entry->root_label;
    }
    return {};
}

auto summary(HelpTopic topic) -> std::string_view {
    if (const auto* entry = find_by_topic(topic); entry != nullptr) {
        return entry->summary;
    }
    return {};
}

auto usage_line(HelpTopic topic) -> std::string_view {
    if (const auto* entry = find_by_topic(topic); entry != nullptr) {
        return entry->usage_line;
    }
    return {};
}

auto help_text(HelpTopic topic) -> std::string_view {
    if (const auto* entry = find_by_topic(topic); entry != nullptr) {
        return entry->help_text;
    }
    return {};
}

auto matches(std::string_view name, HelpTopic topic) -> bool {
    if (const auto* entry = find_by_topic(topic); entry != nullptr) {
        if (name == entry->canonical_name) {
            return true;
        }
        return std::ranges::any_of(entry->aliases, [&](std::string_view alias) {
            return name == alias;
        });
    }
    return false;
}

auto help_topic_from_name(std::string_view name) -> std::optional<HelpTopic> {
    if (const auto* entry = find_by_name(name); entry != nullptr) {
        return entry->topic;
    }
    return std::nullopt;
}

auto build_like_spec(HelpTopic topic) -> const BuildLikeCommandSpec* {
    if (const auto* entry = find_by_topic(topic); entry != nullptr) {
        return entry->build_like;
    }
    return nullptr;
}

auto command_supports_color_option(std::string_view name) -> bool {
    if (const auto* entry = find_by_name(name); entry != nullptr) {
        return entry->supports_color_option;
    }
    return false;
}

}  // namespace cli::catalog
