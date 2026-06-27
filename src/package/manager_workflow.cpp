#include "package/manager_workflow.hpp"

#include "package/backend.hpp"
#include "package/manager_lock.hpp"

namespace package::detail {

auto require_package_management_enabled(const core::Manifest& manifest)
    -> util::Status {
    if (manifest.features.packages) {
        return util::Ok;
    }

    return std::unexpected(util::make_error(
        "Package management is disabled. Set [features].package_manager, "
        "or explicitly set [features].packages = true."));
}

auto resolve_fetch_install(const std::filesystem::path& project_root,
                           const core::Manifest& manifest, bool force_refresh)
    -> util::Status {
    auto graph = GUARD(resolve_with_lock(
        project_root, manifest,
        ResolveOptions{.force_refresh = force_refresh, .features = {}}));
    GUARD(package::fetch(project_root, manifest, graph));
    return package::install(project_root, manifest, graph);
}

auto resolve_and_fetch_only(const std::filesystem::path& project_root,
                            const core::Manifest& manifest, bool force_refresh)
    -> util::Status {
    auto graph = GUARD(resolve_with_lock(
        project_root, manifest,
        ResolveOptions{.force_refresh = force_refresh, .features = {}}));
    return package::fetch(project_root, manifest, graph);
}

}  // namespace package::detail
