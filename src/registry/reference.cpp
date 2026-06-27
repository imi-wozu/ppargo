#include "registry/reference.hpp"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#include "package/home.hpp"
#include "registry/reference_index.hpp"
#include "registry/reference_support.hpp"
#include "util/fs.hpp"
#include "util/process.hpp"

namespace registry::reference {

namespace {

auto registry_name_from_source(const package::ResolvedPackage& package)
    -> util::Result<std::string> {
    if (package.source.rfind("registry+", 0) != 0) {
        return std::unexpected(util::make_error(std::format(
            "Package '{}' does not use a registry source.",
            package.name)));
    }

    const auto registry_name = package.source.substr(9);
    if (registry_name.empty()) {
        return std::unexpected(util::make_error(std::format(
            "Package '{}' has an invalid registry source.",
            package.name)));
    }
    return registry_name;
}

auto artifact_cache_path(std::string_view  registry_name,
                         const package::ResolvedPackage& package)
    -> util::Result<std::filesystem::path> {
    if (!package.artifact.empty()) {
        const auto cache_root = GUARD(package::home::registry_cache_dir());
        const auto artifact_name = std::filesystem::path(package.artifact).filename();
        const auto cache_dir = cache_root / std::string(registry_name);
        std::error_code ec;
        std::filesystem::create_directories(cache_dir, ec);
        if (ec) {
            return std::unexpected(util::make_error("Failed to create registry cache dir: " +
                                       cache_dir.string() + " (" + ec.message() + ")"));
        }
        return cache_dir / artifact_name;
    }
    return detail::make_artifact_path(registry_name, package.name, package.version);
}

auto verify_checksum(const std::filesystem::path& artifact,
                     const package::ResolvedPackage& package) -> util::Status {
    if (package.checksum.empty()) {
        return util::Ok;
    }

    const auto digest = GUARD(detail::checksum_sha256(artifact));
    if (digest != package.checksum) {
        return std::unexpected(util::make_error(std::format(
            "Checksum mismatch for '{}'. Expected {}, got {}.",
            artifact.string(), package.checksum, digest)));
    }
    return util::Ok;
}

}  // namespace

auto resolve_registry_package(const core::Manifest& manifest,
                              std::string_view  registry,
                              std::string_view  name,
                              std::string_view  requirement)
    -> util::Result<package::ResolvedPackage> {
    const auto selected_registry = detail::normalize_registry(manifest, registry);
    const auto records = GUARD(detail::read_index(manifest, selected_registry, name));
    const detail::IndexRecord* selected = nullptr;
    for (const auto& record : records) {
        if (record.yanked ||
            !detail::matches_requirement(record.version, requirement)) {
            continue;
        }
        if (selected == nullptr ||
            detail::semver_greater(record.version, selected->version)) {
            selected = &record;
        }
    }

    if (selected == nullptr) {
        return std::unexpected(util::make_error(std::format(
            "No non-yanked version for '{}' matches requirement '{}' in registry '{}'.",
            name, requirement, selected_registry)));
    }

    package::ResolvedPackage pkg{};
    pkg.name = std::string(name);
    pkg.version = selected->version;
    pkg.source = "registry+" + selected_registry;
    pkg.checksum = selected->checksum;
    pkg.artifact = selected->artifact;
    pkg.dependencies = selected->dependencies;
    pkg.dependency_specs.reserve(selected->dependency_specs.size());
    for (const auto& dependency : selected->dependency_specs) {
        pkg.dependency_specs.push_back(package::ResolvedDependency{
            .name = dependency.name,
            .spec = dependency.spec,
        });
    }
    return pkg;
}

auto fetch_registry_package(const core::Manifest& manifest,
                            const package::ResolvedPackage& package) -> util::Status {
    const auto registry_name = GUARD(registry_name_from_source(package));
    const auto endpoints = GUARD(detail::resolve_registry_endpoints(manifest, registry_name));
    const auto artifact = GUARD(artifact_cache_path(endpoints.name, package));

    std::error_code ec;
    const auto artifact_exists = std::filesystem::exists(artifact, ec) && !ec;
    if (artifact_exists) {
        auto digest = detail::checksum_sha256(artifact);
        if (digest && (package.checksum.empty() || *digest == package.checksum)) {
            // cache hit
        } else {
            std::filesystem::remove(artifact, ec);
        }
    }

    if (!std::filesystem::exists(artifact, ec) || ec) {
        const auto download_url = detail::join_url(
            endpoints.api,
            std::format("crates/{}/{}/download", package.name, package.version));
        auto response = GUARD(detail::http_download(download_url, artifact));
        if (response.status_code < 200 || response.status_code >= 300) {
            return std::unexpected(util::make_error(std::format(
                "Failed to download '{}@{}' from registry '{}': {}",
                package.name, package.version, endpoints.name,
                detail::http_error_message(response, "download failed"))));
        }
    }

    GUARD(verify_checksum(artifact, package));

    const auto src_base = GUARD(package::home::registry_src_dir());
    const auto extract_dir = src_base / (package.name + "-" + package.version);
    if (std::filesystem::exists(extract_dir, ec) && !ec) {
        return util::Ok;
    }

    const auto staged_extract_dir =
        util::fs::temporary_path_for(extract_dir, "extract");
    std::filesystem::create_directories(staged_extract_dir, ec);
    if (ec) {
        return std::unexpected(util::make_error(
            "Failed to create extraction directory: " +
            staged_extract_dir.string() + " (" + ec.message() + ")"));
    }

    util::process::RunOptions options{};
    options.capture_output = true;
    options.merge_stderr = true;
    options.stdin_mode = util::process::StdinMode::Null;
    const std::vector<std::string> args{
        "-xzf", artifact.string(), "-C", staged_extract_dir.string()};
    auto extract = util::process::run_result("tar", args, options);
    if (!extract) {
        std::filesystem::remove_all(staged_extract_dir, ec);
        return std::unexpected(std::move(extract.error()));
    }
    if (extract->exit_code != 0) {
        std::filesystem::remove_all(staged_extract_dir, ec);
        return std::unexpected(util::make_error("Failed to extract package artifact: " +
            artifact.string()));
    }

    return util::fs::publish_staged_directory(staged_extract_dir, extract_dir);
}

}  // namespace registry::reference




