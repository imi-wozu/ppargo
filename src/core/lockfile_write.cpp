#include "core/lockfile.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <span>

#include "util/containers.hpp"
#include "util/fs.hpp"

namespace {

auto quote(std::string_view  input) -> std::string {
    std::string escaped;
    escaped.reserve(input.size() + 2);
    for (char ch : input) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return "\"" + escaped + "\"";
}

auto stringify_array(std::span<const std::string>  values) -> std::string {
    const auto canonical =
        util::sorted_unique(std::vector<std::string>(values.begin(), values.end()));
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < canonical.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << quote(canonical[i]);
    }
    out << "]";
    return out.str();
}

auto module_support_to_string(core::LockModuleSupport support) -> std::string {
    switch (support) {
        case core::LockModuleSupport::None:
            return "none";
        case core::LockModuleSupport::Named:
            return "named";
        case core::LockModuleSupport::HeaderUnits:
            return "header-units";
    }
    return "none";
}

auto sorted_packages(std::vector<core::LockPackage> packages)
    -> std::vector<core::LockPackage> {
    std::sort(packages.begin(), packages.end(),
              [](const core::LockPackage& lhs, const core::LockPackage& rhs) {
                  return lhs.id < rhs.id;
              });
    return packages;
}

}  // namespace

namespace core {

auto to_toml(const Lockfile& lockfile) -> util::Result<std::string> {
    std::ostringstream out;
    out << "version = " << lockfile.version << "\n\n";
    out << "[metadata]\n";
    out << "manifest_fingerprint = " << quote(lockfile.manifest_fingerprint) << "\n";
    out << "package_manager = " << quote(lockfile.package_manager) << "\n";
    out << "root_features = " << stringify_array(lockfile.root_features) << "\n";
    out << "all_features = " << (lockfile.all_features ? "true" : "false")
        << "\n";
    out << "no_default_features = "
        << (lockfile.no_default_features ? "true" : "false") << "\n";
    out << "module_mode_enabled = " << (lockfile.module_mode_enabled ? "true" : "false")
        << "\n";
    out << "compiler = " << quote(lockfile.compiler) << "\n";
    out << "scan_deps_fingerprint = " << quote(lockfile.scan_deps_fingerprint) << "\n\n";

    for (const auto& package : sorted_packages(lockfile.packages)) {
        out << "[[package]]\n";
        out << "id = "
            << quote(package.id.empty()
                         ? make_lock_package_id(package.name, package.version,
                                                package.source)
                         : package.id)
            << "\n";
        out << "name = " << quote(package.name) << "\n";
        out << "version = " << quote(package.version) << "\n";
        out << "source = " << quote(package.source) << "\n";
        out << "checksum = " << quote(package.checksum) << "\n";
        out << "artifact = " << quote(package.artifact) << "\n";
        out << "dependencies = " << stringify_array(package.dependencies) << "\n";
        out << "unresolved_dependencies = "
            << stringify_array(package.unresolved_dependencies) << "\n";
        out << "active_features = "
            << stringify_array(package.active_features) << "\n";
        out << "module_support = " << quote(module_support_to_string(package.module_support))
            << "\n";
        out << "module_map_path = " << quote(package.module_map_path) << "\n";
        out << "bmi_include_dirs = " << stringify_array(package.bmi_include_dirs) << "\n";
        out << "exported_modules = " << stringify_array(package.exported_modules) << "\n";
        out << "module_metadata_fingerprint = "
            << quote(package.module_metadata_fingerprint) << "\n\n";
    }

    return out.str();
}

auto save_lockfile(const std::filesystem::path& path, const Lockfile& lockfile)
    -> util::Status {
    auto toml = to_toml(lockfile);
    if (!toml) {
        return std::unexpected(toml.error());
    }
    return util::fs::atomic_write_text_result(path, *toml);
}

}  // namespace core


