#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/manifest.hpp"
#include "util/result.hpp"


namespace package::vcpkg {

struct PackageInfo {
    std::string name;
    std::string version;
    std::string description;
};

auto vcpkg_executable(const core::Manifest& manifest)
    -> util::Result<std::filesystem::path>;
auto ensure_vcpkg_manifest(const std::filesystem::path& project_root) -> util::Status;
auto parse_package_list(const std::string& output)
    -> util::Result<std::vector<PackageInfo>>;
auto search_packages(const core::Manifest& manifest,
                     const std::string& package)
    -> util::Result<std::vector<PackageInfo>>;
auto upsert_dependency(const std::filesystem::path& project_root,
                       const std::string& package_name) -> util::Status;
auto install_dependencies(const std::filesystem::path& project_root,
                          const core::Manifest& manifest) -> util::Status;

}  // namespace package::vcpkg



