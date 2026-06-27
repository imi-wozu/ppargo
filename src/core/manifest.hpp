#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "util/result.hpp"

namespace core {

enum class DependencySource {
    Registry,
    Path,
    Git,
};

struct DependencySpec {
    DependencySource source = DependencySource::Registry;
    std::string version;
    std::filesystem::path path;
    std::string git;
    std::string rev;
    std::string registry;
    std::vector<std::string> features;
    bool optional = false;
    bool default_features = true;
};

struct RegistryConfig {
    std::string index;
    std::string api;
};

struct Package {
    std::string name;
    std::string version;
    std::string edition;
};

struct Workspace {
    bool enabled = false;
    std::vector<std::filesystem::path> members;
    std::vector<std::filesystem::path> exclude;
};

struct Toolchain {
    std::string compiler;
};

struct Features {
    bool packages = false;
    std::string package_manager;
    std::filesystem::path vcpkg_root;
    std::map<std::string, std::vector<std::string>> package_features;
};

struct Build {
    std::filesystem::path source_dir;
    std::vector<std::filesystem::path> include_dirs;
    std::vector<std::string> exclude;
    std::filesystem::path output_dir;
    std::string binary_name;
    bool modules = false;
    std::vector<std::string> module_interface_exts;
    std::filesystem::path module_output_dir;
    int aggressive_tu_threshold = 24;
    int aggressive_stale_threshold = 8;
    int pch_scan_lines = 200;
    double pch_frequency_threshold = 0.60;
    int pch_max_headers = 40;
    int depscan_timeout_ms = 10000;
};

using DependencyMap = std::map<std::string, DependencySpec>;

struct Manifest {
    bool package_defined = true;
    Package package;
    DependencyMap dependencies;
    DependencyMap dev_dependencies;
    DependencyMap build_dependencies;
    std::map<std::string, RegistryConfig> registries;
    Workspace workspace;
    Toolchain toolchain;
    Features features;
    Build build;
};

struct ProjectContext {
    std::filesystem::path root;
    Manifest manifest;
};

auto default_manifest(std::string_view package_name) -> Manifest;
auto load_manifest(const std::filesystem::path& path) -> util::Result<Manifest>;
auto save_manifest(const std::filesystem::path& path, const Manifest& manifest)
    -> util::Status;
auto to_toml(const Manifest& manifest) -> util::Result<std::string>;
auto dependency_source_name(DependencySource source) -> std::string;

auto load_project_context() -> util::Result<ProjectContext>;
auto load_project_context_from_manifest(const std::filesystem::path& manifest_path)
    -> util::Result<ProjectContext>;

}  // namespace core
