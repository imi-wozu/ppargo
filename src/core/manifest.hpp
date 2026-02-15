#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "util/result.hpp"


namespace core {

struct Package {
    std::string name;
    std::string version;
    std::string edition;
};

struct Toolchain {
    std::string compiler;
    std::string linker;
};

struct Features {
    bool packages;
    std::string package_manager;
    std::filesystem::path vcpkg_root;
    bool has_vcpkg_root;
};

struct Build {
    std::filesystem::path source_dir;
    std::vector<std::filesystem::path> include_dirs;
    std::vector<std::string> exclude;
    std::filesystem::path output_dir;
    std::string binary_name;
};

struct Manifest {
    Package package;
    std::map<std::string, std::string> dependencies;
    Toolchain toolchain;
    Features features;
    Build build;
};

auto default_manifest(const std::string& package_name) -> Manifest;
auto load_manifest(const std::filesystem::path& path) -> util::Result<Manifest>;
auto save_manifest(const std::filesystem::path& path,
                   const Manifest& manifest) -> util::Status;
auto to_toml(const Manifest& manifest) -> util::Result<std::string>;

}  // namespace core



