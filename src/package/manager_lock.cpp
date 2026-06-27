#include "package/manager_lock.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "package/resolver/resolver.hpp"
#include "util/containers.hpp"

namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

auto stable_hash_text(std::string_view text) -> std::string {
    std::uint64_t hash = kFnvOffset;
    for (const unsigned char ch : text) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= kFnvPrime;
    }
    return std::format("{:016x}", hash);
}

auto selected_package_manager(const core::Manifest& manifest) -> std::string {
    if (!manifest.features.package_manager.empty()) {
        return manifest.features.package_manager;
    }
    return "none";
}

auto feature_options_fingerprint_text(const package::FeatureOptions& features)
    -> std::string {
    auto requested = util::sorted_unique(features.requested);
    std::string text = "features:";
    text += features.all_features ? "all=1;" : "all=0;";
    text += features.no_default_features ? "default=0;" : "default=1;";
    for (const auto& feature : requested) {
        text += feature;
        text.push_back(';');
    }
    return text;
}

}  // namespace

namespace package::detail {

auto manifest_fingerprint(const core::Manifest& manifest,
                          const package::FeatureOptions& features)
    -> util::Result<std::string> {
    auto toml = GUARD(core::to_toml(manifest));
    toml += "\n";
    toml += feature_options_fingerprint_text(features);
    return stable_hash_text(toml);
}

auto graph_from_lock(const core::Lockfile& lockfile) -> package::ResolvedGraph {
    package::ResolvedGraph graph{};
    graph.packages.reserve(lockfile.packages.size());

    for (const auto& package : lockfile.packages) {
        package::ResolvedPackage resolved{};
        resolved.name = package.name;
        resolved.version = package.version;
        resolved.source = package.source;
        resolved.checksum = package.checksum;
        resolved.artifact = package.artifact;
        resolved.dependencies.reserve(package.dependencies.size() +
                                      package.unresolved_dependencies.size());
        for (const auto& dependency_id : package.dependencies) {
            resolved.dependencies.push_back(
                core::lock_package_name_from_id(dependency_id));
        }
        resolved.dependencies.insert(resolved.dependencies.end(),
                                     package.unresolved_dependencies.begin(),
                                     package.unresolved_dependencies.end());
        resolved.dependencies = util::sorted_unique(std::move(resolved.dependencies));
        resolved.active_features = package.active_features;
        switch (package.module_support) {
            case core::LockModuleSupport::None:
                resolved.module_support = package::ModuleSupport::None;
                break;
            case core::LockModuleSupport::Named:
                resolved.module_support = package::ModuleSupport::Named;
                break;
            case core::LockModuleSupport::HeaderUnits:
                resolved.module_support = package::ModuleSupport::HeaderUnits;
                break;
        }
        resolved.module_map_path = package.module_map_path;
        resolved.bmi_include_dirs = package.bmi_include_dirs;
        resolved.exported_modules = package.exported_modules;
        resolved.module_metadata_fingerprint = package.module_metadata_fingerprint;
        graph.packages.push_back(std::move(resolved));
    }

    return graph;
}

auto lock_from_graph(std::string_view  fingerprint, const core::Manifest& manifest,
                     const package::ResolvedGraph& graph,
                     const package::FeatureOptions& features) -> core::Lockfile {
    core::Lockfile lockfile{};
    lockfile.version = core::kCurrentLockfileVersion;
    lockfile.manifest_fingerprint = fingerprint;
    lockfile.package_manager = selected_package_manager(manifest);
    lockfile.root_features = util::sorted_unique(features.requested);
    lockfile.all_features = features.all_features;
    lockfile.no_default_features = features.no_default_features;
    lockfile.module_mode_enabled = manifest.build.modules;
    lockfile.compiler = manifest.toolchain.compiler;
    lockfile.scan_deps_fingerprint =
        stable_hash_text(manifest.toolchain.compiler + "|clang-scan-deps");
    lockfile.packages.reserve(graph.packages.size());

    std::unordered_map<std::string, std::vector<std::string>> ids_by_name;
    ids_by_name.reserve(graph.packages.size());
    for (const auto& package : graph.packages) {
        ids_by_name[package.name].push_back(core::make_lock_package_id(
            package.name, package.version, package.source));
    }
    for (auto& [name, ids] : ids_by_name) {
        (void)name;
        ids = util::sorted_unique(std::move(ids));
    }

    for (const auto& package : graph.packages) {
        core::LockPackage lock_package{};
        lock_package.id =
            core::make_lock_package_id(package.name, package.version, package.source);
        lock_package.name = package.name;
        lock_package.version = package.version;
        lock_package.source = package.source;
        lock_package.checksum = package.checksum;
        lock_package.artifact = package.artifact;
        for (const auto& dependency_name : package.dependencies) {
            const auto found = ids_by_name.find(dependency_name);
            if (found != ids_by_name.end() && found->second.size() == 1) {
                lock_package.dependencies.push_back(found->second.front());
            } else {
                lock_package.unresolved_dependencies.push_back(dependency_name);
            }
        }
        lock_package.dependencies =
            util::sorted_unique(std::move(lock_package.dependencies));
        lock_package.unresolved_dependencies =
            util::sorted_unique(std::move(lock_package.unresolved_dependencies));
        lock_package.active_features =
            util::sorted_unique(package.active_features);
        switch (package.module_support) {
            case package::ModuleSupport::None:
                lock_package.module_support = core::LockModuleSupport::None;
                break;
            case package::ModuleSupport::Named:
                lock_package.module_support = core::LockModuleSupport::Named;
                break;
            case package::ModuleSupport::HeaderUnits:
                lock_package.module_support = core::LockModuleSupport::HeaderUnits;
                break;
        }
        lock_package.module_map_path = package.module_map_path;
        lock_package.bmi_include_dirs =
            util::sorted_unique(package.bmi_include_dirs);
        lock_package.exported_modules =
            util::sorted_unique(package.exported_modules);
        lock_package.module_metadata_fingerprint =
            package.module_metadata_fingerprint;
        lockfile.packages.push_back(std::move(lock_package));
    }

    std::sort(lockfile.packages.begin(), lockfile.packages.end(),
              [](const core::LockPackage& lhs, const core::LockPackage& rhs) {
                  return lhs.id < rhs.id;
              });
    return lockfile;
}

auto resolve_with_lock(const std::filesystem::path& project_root,
                       const core::Manifest& manifest,
                       const ResolveOptions& options)
    -> util::Result<package::ResolvedGraph> {
    const auto lock_path{project_root / "ppargo.lock"};
    const auto fingerprint = GUARD(manifest_fingerprint(manifest, options.features));
    auto loaded_lock{core::load_lockfile(lock_path)};
    if (loaded_lock && loaded_lock->version == core::kCurrentLockfileVersion &&
        loaded_lock->manifest_fingerprint == fingerprint) {
        return graph_from_lock(*loaded_lock);
    }

    if (options.locked || options.offline) {
        if (!loaded_lock) {
            return std::unexpected(util::make_error(
                "Lockfile is required by the selected build mode, but ppargo.lock is missing."));
        }
        return std::unexpected(util::make_error(
            "Lockfile does not match the current manifest, but the selected build mode forbids updating it."));
    }

    if (!options.force_refresh && loaded_lock &&
        loaded_lock->version == core::kCurrentLockfileVersion &&
        loaded_lock->manifest_fingerprint == fingerprint) {
        return graph_from_lock(*loaded_lock);
    }

    auto graph =
        GUARD(package::resolver::resolve(project_root, manifest, options.features));
    auto lockfile{lock_from_graph(fingerprint, manifest, graph, options.features)};
    GUARD(core::save_lockfile(lock_path, lockfile));
    return graph;
}

}  // namespace package::detail
