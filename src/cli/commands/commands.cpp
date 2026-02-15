#include "cli/commands/commands.hpp"

#include <format>

#include "core/manifest.hpp"
#include "core/paths.hpp"
#include "util/output.hpp"
#include "util/result.hpp"

namespace cli {

namespace {

auto print_help() -> void {
    util::output::write_help(
        "ppargo C++ build system and package manager\n\n"
        "Usage: argo <COMMAND>\n\n"
        "Commands:\n"
        "  new <name>      Create a new ppargo package\n"
        "  init            Create a new ppargo package in current directory\n"
        "  add <package>   Add package dependency via configured package "
        "manager\n"
        "  build|b [-r]     Build current package\n"
        "  check|c [-r]     Check current package (no link/output binary)\n"
        "  run|r [-r]       Build and run current package\n"
        "  version         Print version\n"
        "  help            Print this help\n");
}

}  // namespace

auto VersionCommand::execute() const -> util::Status {
    auto root = TRY(core::find_project_root());
    auto manifest = TRY(core::load_manifest(root / "ppargo.toml"));
    util::output::line(
        util::output::Stream::Stdout,
        std::format("{} {}", manifest.package.name, manifest.package.version));
    return util::Ok;
}

auto HelpCommand::execute() const -> util::Status {
    print_help();
    return util::Ok;
}

}  // namespace cli
