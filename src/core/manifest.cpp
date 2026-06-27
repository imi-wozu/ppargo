#include "core/manifest.hpp"

#include <string>

#include "core/paths.hpp"

namespace core {

auto dependency_source_name(DependencySource source) -> std::string {
    switch (source) {
        case DependencySource::Registry:
            return "registry";
        case DependencySource::Path:
            return "path";
        case DependencySource::Git:
            return "git";
    }
    return "unknown";
}

auto load_project_context() -> util::Result<ProjectContext> {
    auto root = GUARD(find_project_root());
    return ProjectContext{
        .root = std::move(root),
        .manifest = GUARD(load_manifest(root / "ppargo.toml"))};
}

auto load_project_context_from_manifest(const std::filesystem::path& manifest_path)
    -> util::Result<ProjectContext> {
    std::error_code ec;
    const auto absolute_manifest = std::filesystem::absolute(manifest_path, ec);
    if (ec) {
        return std::unexpected(util::make_error(
            "Failed to resolve manifest path: " + manifest_path.string() + " (" +
            ec.message() + ")"));
    }
    const auto root = absolute_manifest.parent_path();
    return ProjectContext{
        .root = root,
        .manifest = GUARD(load_manifest(absolute_manifest)),
    };
}

}  // namespace core
