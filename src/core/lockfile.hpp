#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "util/result.hpp"

namespace core {

inline constexpr int kCurrentLockfileVersion = 2;

enum class LockModuleSupport {
    None,
    Named,
    HeaderUnits,
};

struct LockPackage {
    std::string id;
    std::string name;
    std::string version;
    std::string source;
    std::string checksum;
    std::string artifact;
    std::vector<std::string> dependencies;
    std::vector<std::string> unresolved_dependencies;
    std::vector<std::string> active_features;
    LockModuleSupport module_support{LockModuleSupport::None};
    std::string module_map_path;
    std::vector<std::string> bmi_include_dirs;
    std::vector<std::string> exported_modules;
    std::string module_metadata_fingerprint;
};

struct Lockfile {
    int version{kCurrentLockfileVersion};
    std::string manifest_fingerprint;
    std::string package_manager;
    std::vector<std::string> root_features;
    bool all_features{false};
    bool no_default_features{false};
    bool module_mode_enabled{false};
    std::string compiler;
    std::string scan_deps_fingerprint;
    std::vector<LockPackage> packages;
};

auto make_lock_package_id(std::string_view name, std::string_view version,
                          std::string_view source) -> std::string;
auto lock_package_name_from_id(std::string_view id) -> std::string;

auto load_lockfile(const std::filesystem::path& path) -> util::Result<Lockfile>;
auto save_lockfile(const std::filesystem::path& path, const Lockfile& lockfile)
    -> util::Status;
auto to_toml(const Lockfile& lockfile) -> util::Result<std::string>;

}  // namespace core
