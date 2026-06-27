#include "cli/commands/commands.hpp"

#include <string_view>

#include "cli/commands/common.hpp"
#include "core/manifest.hpp"
#include "util/output.hpp"
#include "util/result.hpp"

namespace cli {
namespace {

auto set_package_manager(std::string_view package_manager) -> util::Status {
    auto context = GUARD(commands::common::load_project_context(std::nullopt));
    const auto manifest_path = context.manifest_path;
    auto manifest = GUARD(core::load_manifest(manifest_path));

    manifest.features.packages = true;
    manifest.features.package_manager = std::string(package_manager);

    GUARD(core::save_manifest(manifest_path, manifest));
    util::output::argo_status(
        "Updated", util::output::Color::Green,
        std::format("set [features].package_manager = \"{}\"", package_manager));
    return util::Ok;
}

}  // namespace

auto VcpkgCommand::execute() const -> util::Status {
    return set_package_manager("vcpkg");
}

auto PpargoCommand::execute() const -> util::Status {
    return set_package_manager("ppargo");
}

}  // namespace cli
