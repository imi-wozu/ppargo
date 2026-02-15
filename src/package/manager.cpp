#include "package/manager.hpp"

#include <format>
#include <regex>

#include "core/paths.hpp"
#include "core/manifest.hpp"
#include "package/vcpkg.hpp"


namespace package {

namespace {

auto is_valid_package_name(const std::string& package_name) {
    static const std::regex pattern("^[A-Za-z0-9][A-Za-z0-9+_.-]*$");
    return std::regex_match(package_name, pattern);
}

}  // namespace

auto install_dependencies(const std::filesystem::path& project_root,
                          const core::Manifest& manifest) -> util::Status {
    if (!manifest.features.packages) {
        return util::Ok;
    }
    if (manifest.features.package_manager == "vcpkg") {
        return vcpkg::install_dependencies(project_root, manifest);
    }

    return std::unexpected(std::format(
        "Unsupported package manager: {}", manifest.features.package_manager));
}

auto add_dependency(const std::filesystem::path& project_root,
                    const std::string& package_name) -> util::Status {
    if (!is_valid_package_name(package_name)) {
        return std::unexpected(
            "Validation Error: Invalid package name. Allowed characters: "
            "letters, digits, '+', '_', '-', '.'.");
    }

    const auto manifest_path = project_root / "ppargo.toml";
    auto manifest_result = core::load_manifest(manifest_path);
    if (!manifest_result) {
        return std::unexpected(manifest_result.error());
    }
    auto manifest = std::move(*manifest_result);

    if (!manifest.features.packages) {
        return std::unexpected(
            "Package management is disabled. Set [features].packages = true.");
    }
    if (manifest.features.package_manager != "vcpkg") {
        return std::unexpected(std::format(
            "Unsupported package manager for `add`: {}",
            manifest.features.package_manager));
    }

    auto matches_result = vcpkg::search_packages(manifest, package_name);
    if (!matches_result) {
        return std::unexpected(matches_result.error());
    }
    const auto matches = std::move(*matches_result);
    if (matches.empty()) {
        return std::unexpected(
            std::format("Package '{}' not found in the vcpkg registry.", package_name));
    }

    std::string resolved_name = package_name;
    std::string resolved_version = matches.front().version;
    for (const auto& pkg : matches) {
        if (pkg.name == package_name) {
            resolved_name = pkg.name;
            resolved_version = pkg.version;
            break;
        }
    }

    manifest.dependencies[resolved_name] = resolved_version;
    auto save = core::save_manifest(manifest_path, manifest);
    if (!save) {
        return std::unexpected(save.error());
    }

    auto ensure = vcpkg::ensure_vcpkg_manifest(project_root);
    if (!ensure) {
        return std::unexpected(ensure.error());
    }
    auto upsert = vcpkg::upsert_dependency(project_root, resolved_name);
    if (!upsert) {
        return std::unexpected(upsert.error());
    }
    auto install = vcpkg::install_dependencies(project_root, manifest);
    if (!install) {
        return std::unexpected(install.error());
    }
    return util::Ok;
}

}  // namespace package



