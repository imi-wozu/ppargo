#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/manifest.hpp"
#include "util/result.hpp"

namespace registry::reference::detail {

struct IndexDependency {
    std::string name;
    core::DependencySpec spec;
};

struct IndexRecord {
    std::string version;
    std::string checksum;
    std::string artifact;
    bool yanked = false;
    std::vector<std::string> dependencies;
    std::vector<IndexDependency> dependency_specs;
    std::vector<std::string> owners;
};

auto normalize_registry(const core::Manifest& manifest, std::string_view  registry)
    -> std::string;

auto read_index(const core::Manifest& manifest, std::string_view  registry_name,
                std::string_view  package_name)
    -> util::Result<std::vector<IndexRecord>>;
auto semver_greater(std::string_view  lhs, std::string_view  rhs) -> bool;
auto matches_requirement(std::string_view  version, std::string_view  requirement)
    -> bool;

auto checksum_sha256(const std::filesystem::path& file) -> util::Result<std::string>;

auto make_artifact_path(std::string_view  registry, std::string_view  package_name,
                        std::string_view  version)
    -> util::Result<std::filesystem::path>;

}  // namespace registry::reference::detail
