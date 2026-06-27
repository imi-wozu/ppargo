#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/manifest.hpp"
#include "util/result.hpp"

namespace package {

enum class ModuleSupport {
    None,
    Named,
    HeaderUnits,
};

struct ResolvedDependency {
    std::string name;
    core::DependencySpec spec;
};

struct ResolvedPackage {
    std::string name;
    std::string version;
    std::string source;
    std::string checksum;
    std::string artifact;
    std::vector<std::string> dependencies;
    std::vector<ResolvedDependency> dependency_specs;
    std::vector<std::string> active_features;
    ModuleSupport module_support{ModuleSupport::None};
    std::string module_map_path;
    std::vector<std::string> bmi_include_dirs;
    std::vector<std::string> exported_modules;
    std::string module_metadata_fingerprint;
};

struct ResolvedGraph {
    std::vector<ResolvedPackage> packages;
};

struct FeatureOptions {
    std::vector<std::string> requested;
    bool all_features = false;
    bool no_default_features = false;
};

struct YankRequest {
    std::string version;
    bool undo = false;
};

auto backend_name(const core::Manifest& manifest) -> util::Result<std::string>;

auto resolve(const std::filesystem::path& project_root,
             const core::Manifest& manifest,
             const FeatureOptions& features = {}) -> util::Result<ResolvedGraph>;
auto fetch(const std::filesystem::path& project_root,
           const core::Manifest& manifest,
           const ResolvedGraph& graph) -> util::Status;
auto install(const std::filesystem::path& project_root,
             const core::Manifest& manifest,
             const ResolvedGraph& graph) -> util::Status;

auto publish(const std::filesystem::path& project_root,
             const core::Manifest& manifest) -> util::Status;
auto yank(const std::filesystem::path& project_root,
          const core::Manifest& manifest,
          const YankRequest& request) -> util::Status;
auto owner_add(const std::filesystem::path& project_root,
               const core::Manifest& manifest,
               std::string_view  owner) -> util::Status;
auto owner_remove(const std::filesystem::path& project_root,
                  const core::Manifest& manifest,
                  std::string_view  owner) -> util::Status;
auto auth_login(const std::filesystem::path& project_root,
                const core::Manifest& manifest,
                std::string_view  registry,
                std::string_view  token) -> util::Status;
auto auth_logout(const std::filesystem::path& project_root,
                 const core::Manifest& manifest,
                 std::string_view  registry) -> util::Status;

}  // namespace package
