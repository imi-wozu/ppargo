#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/manifest.hpp"
#include "package/backend.hpp"
#include "util/result.hpp"

namespace package::vcpkg {

struct PackageInfo {
    std::string name;
    std::string version;
    std::string description;
};

auto vcpkg_executable(const core::Manifest& manifest)
    -> util::Result<std::filesystem::path>;
auto ensure_vcpkg_manifest(const std::filesystem::path& project_root)
    -> util::Status;
auto parse_package_list(std::string_view output)
    -> util::Result<std::vector<PackageInfo>>;
auto search_packages(const core::Manifest& manifest, std::string_view package)
    -> util::Result<std::vector<PackageInfo>>;
auto sync_dependencies(const std::filesystem::path& project_root,
                       const package::ResolvedGraph& graph) -> util::Status;
auto required_packages_installed(const std::filesystem::path& project_root,
                                 const package::ResolvedGraph& graph)
    -> util::Result<bool>;
auto install_dependencies(const std::filesystem::path& project_root,
                          const core::Manifest& manifest,
                          const package::ResolvedGraph& graph) -> util::Status;

}  // namespace package::vcpkg
