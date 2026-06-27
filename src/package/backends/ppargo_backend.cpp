#include "package/backends/ppargo_backend.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/paths.hpp"
#include "package/home.hpp"
#include "registry/reference.hpp"
#include "util/fs.hpp"
#include "util/process.hpp"
#include "util/result.hpp"

namespace {

auto selected_registry(const core::Manifest& manifest) -> std::string {
    if (manifest.registries.contains("default")) {
        return "default";
    }
    return "ppargo";
}

auto requirement_matches(std::string_view resolved,
                         std::string_view requirement) -> bool {
    if (requirement.empty() || requirement == "*") {
        return true;
    }
    if (resolved == requirement ||
        resolved.rfind(std::string(requirement) + "#", 0) == 0) {
        return true;
    }
    if (requirement.front() == '^') {
        const auto req = requirement.substr(1);
        const auto dot = req.find('.');
        const auto major = dot == std::string::npos ? req : req.substr(0, dot);
        return resolved.rfind(std::string(major) + ".", 0) == 0;
    }
    return false;
}

struct FeatureResolution {
    std::unordered_set<std::string> enabled_optional_dependencies;
    std::unordered_map<std::string, std::vector<std::string>> dependency_features;
    std::vector<std::string> active_root_features;
};

auto sorted_unique_strings(std::vector<std::string> values)
    -> std::vector<std::string> {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

auto append_dependency_feature(FeatureResolution& resolution,
                               std::string dependency,
                               std::string feature) -> void {
    auto& features = resolution.dependency_features[std::move(dependency)];
    features.push_back(std::move(feature));
    features = sorted_unique_strings(std::move(features));
}

auto resolve_feature_token(const core::Manifest& manifest,
                           FeatureResolution& resolution,
                           std::string_view feature,
                           std::unordered_set<std::string>& resolving)
    -> util::Status {
    const auto feature_name = std::string(feature);
    if (const auto slash = feature_name.find('/'); slash != std::string::npos) {
        const auto dependency = feature_name.substr(0, slash);
        const auto dependency_feature = feature_name.substr(slash + 1);
        if (dependency.empty() || dependency_feature.empty() ||
            !manifest.dependencies.contains(dependency)) {
            return std::unexpected(util::make_error(
                "Feature Error: unknown dependency feature '" + feature_name +
                "'."));
        }
        if (manifest.dependencies.at(dependency).optional) {
            resolution.enabled_optional_dependencies.insert(dependency);
        }
        append_dependency_feature(resolution, dependency, dependency_feature);
        return util::Ok;
    }

    if (const auto dependency = manifest.dependencies.find(feature_name);
        dependency != manifest.dependencies.end() && dependency->second.optional) {
        resolution.enabled_optional_dependencies.insert(feature_name);
        return util::Ok;
    }

    const auto defined = manifest.features.package_features.find(feature_name);
    if (defined == manifest.features.package_features.end()) {
        return std::unexpected(util::make_error(
            "Feature Error: unknown feature '" + feature_name + "'."));
    }

    if (!resolving.insert(feature_name).second) {
        return std::unexpected(util::make_error(
            "Feature Error: feature cycle involving '" + feature_name + "'."));
    }
    resolution.active_root_features.push_back(feature_name);
    for (const auto& child : defined->second) {
        GUARD(resolve_feature_token(manifest, resolution, child, resolving));
    }
    resolving.erase(feature_name);
    return util::Ok;
}

auto resolve_root_features(const core::Manifest& manifest,
                           const package::FeatureOptions& options)
    -> util::Result<FeatureResolution> {
    FeatureResolution resolution;
    std::unordered_set<std::string> resolving;

    if (options.all_features) {
        for (const auto& [name, values] : manifest.features.package_features) {
            (void)values;
            GUARD(resolve_feature_token(manifest, resolution, name, resolving));
        }
        for (const auto& [name, spec] : manifest.dependencies) {
            if (spec.optional) {
                resolution.enabled_optional_dependencies.insert(name);
            }
        }
    } else {
        if (!options.no_default_features &&
            manifest.features.package_features.contains("default")) {
            GUARD(resolve_feature_token(manifest, resolution, "default",
                                       resolving));
        }
        for (const auto& requested : options.requested) {
            GUARD(resolve_feature_token(manifest, resolution, requested,
                                       resolving));
        }
    }

    resolution.active_root_features =
        sorted_unique_strings(std::move(resolution.active_root_features));
    return resolution;
}

auto features_for_dependency(const core::DependencySpec& spec,
                             const std::vector<std::string>& forwarded)
    -> std::vector<std::string> {
    auto features = spec.features;
    features.insert(features.end(), forwarded.begin(), forwarded.end());
    return sorted_unique_strings(std::move(features));
}

auto ensure_git_checkout(const package::ResolvedPackage& package)
    -> util::Status {
    if (package.source.rfind("git+", 0) != 0) {
        return util::Ok;
    }

    const auto hash = std::to_string(std::hash<std::string>{}(package.source));
    const auto checkouts = GUARD(package::home::git_checkout_dir());
    const auto checkout_dir = checkouts / (package.name + "-" + hash);

    std::error_code ec;
    if (std::filesystem::exists(checkout_dir, ec) && !ec) {
        return util::Ok;
    }

    const auto source_no_prefix = package.source.substr(4);
    const auto hash_pos = source_no_prefix.find('#');
    const auto git_url = hash_pos == std::string::npos
                             ? source_no_prefix
                             : source_no_prefix.substr(0, hash_pos);
    const auto rev = hash_pos == std::string::npos
                         ? std::string{}
                         : source_no_prefix.substr(hash_pos + 1);

    const auto staged_checkout_dir =
        util::fs::temporary_path_for(checkout_dir, "clone");
    std::vector<std::string> clone_args{"clone", "--depth", "1", git_url,
                                        staged_checkout_dir.string()};
    auto clone_result =
        util::process::run_result("git", clone_args, util::process::RunOptions{});
    if (!clone_result) {
        std::filesystem::remove_all(staged_checkout_dir, ec);
        return std::unexpected(std::move(clone_result.error()));
    }
    if (clone_result->exit_code != 0) {
        std::filesystem::remove_all(staged_checkout_dir, ec);
        return std::unexpected(
            util::make_error("Failed to clone git dependency: " + git_url));
    }

    if (!rev.empty()) {
        std::vector<std::string> checkout_args{"-C", staged_checkout_dir.string(),
                                               "checkout", rev};
        auto checkout_result = util::process::run_result(
            "git", checkout_args, util::process::RunOptions{});
        if (!checkout_result) {
            std::filesystem::remove_all(staged_checkout_dir, ec);
            return std::unexpected(std::move(checkout_result.error()));
        }
        if (checkout_result->exit_code != 0) {
            std::filesystem::remove_all(staged_checkout_dir, ec);
            return std::unexpected(
                util::make_error("Failed to checkout revision '" + rev +
                                 "' for git dependency: " + git_url));
        }
    }

    return util::fs::publish_staged_directory(staged_checkout_dir, checkout_dir);
}

auto copy_include_tree(const std::filesystem::path& source_include,
                       const std::filesystem::path& destination_include)
    -> util::Status {
    std::error_code ec;
    if (!std::filesystem::exists(source_include, ec) || ec) {
        return util::Ok;
    }

    std::filesystem::create_directories(destination_include, ec);
    if (ec) {
        return std::unexpected(util::make_error(
            "Failed to create include destination: " +
            destination_include.string() + " (" + ec.message() + ")"));
    }

    std::filesystem::copy(source_include, destination_include,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);
    if (ec) {
        return std::unexpected(util::make_error(
            "Failed to copy include tree from " + source_include.string() +
            " to " + destination_include.string() + " (" + ec.message() + ")"));
    }

    return util::Ok;
}

auto install_package_headers(const std::filesystem::path& project_root,
                             const package::ResolvedPackage& package)
    -> util::Status {
    const auto include_root = project_root / "packages" /
                              core::detect_triplet() / "include" / package.name;

    if (package.source.rfind("registry+", 0) == 0) {
        const auto src_base = GUARD(package::home::registry_src_dir());
        return copy_include_tree(
            src_base / (package.name + "-" + package.version) / "include",
            include_root);
    }

    if (package.source.rfind("path+", 0) == 0) {
        const auto base = std::filesystem::path(package.source.substr(5));
        return copy_include_tree(base / "include", include_root);
    }

    if (package.source.rfind("git+", 0) == 0) {
        const auto hash =
            std::to_string(std::hash<std::string>{}(package.source));
        const auto checkouts = GUARD(package::home::git_checkout_dir());
        return copy_include_tree(
            checkouts / (package.name + "-" + hash) / "include", include_root);
    }

    return util::Ok;
}

auto package_identity(const package::ResolvedPackage& package) -> std::string {
    return package.version + "|" + package.source;
}

auto append_package(package::ResolvedGraph& graph,
                    std::unordered_map<std::string, std::string>& selected,
                    package::ResolvedPackage package) -> util::Result<bool> {
    const auto identity = package_identity(package);
    if (const auto found = selected.find(package.name); found != selected.end()) {
        if (found->second != identity) {
            return std::unexpected(util::make_error(std::format(
                "Resolver Error: dependency '{}' resolves to multiple versions or sources.",
                package.name)));
        }
        for (auto& existing : graph.packages) {
            if (existing.name == package.name) {
                existing.active_features.insert(existing.active_features.end(),
                                                package.active_features.begin(),
                                                package.active_features.end());
                existing.active_features =
                    sorted_unique_strings(std::move(existing.active_features));
                return false;
            }
        }
        return false;
    }

    selected.emplace(package.name, identity);
    graph.packages.push_back(std::move(package));
    return true;
}

auto resolve_dependency(const std::filesystem::path& project_root,
                        const core::Manifest& manifest,
                        package::ResolvedGraph& graph,
                        std::unordered_map<std::string, std::string>& selected,
                        std::string_view name,
                        const core::DependencySpec& spec,
                        std::string_view default_registry,
                        std::vector<std::string> active_features,
                        bool allow_local_sources) -> util::Status {
    if (spec.source == core::DependencySource::Path) {
        if (!allow_local_sources) {
            return std::unexpected(util::make_error(std::format(
                "Resolver Error: registry dependency '{}' cannot use a path source.",
                name)));
        }
        const auto dep_manifest_path = project_root / spec.path / "ppargo.toml";
        auto dep_manifest = GUARD(core::load_manifest(dep_manifest_path));
        if (!requirement_matches(dep_manifest.package.version, spec.version)) {
            return std::unexpected(util::make_error(std::format(
                "Resolver Error: path dependency '{}' resolved to version "
                "'{}' which does not satisfy '{}'.",
                name, dep_manifest.package.version, spec.version)));
        }

        package::ResolvedPackage pkg;
        pkg.name = std::string(name);
        pkg.version = dep_manifest.package.version;
        pkg.source = "path+" + (project_root / spec.path).generic_string();
        pkg.active_features = sorted_unique_strings(std::move(active_features));
        pkg.module_support = package::ModuleSupport::Named;
        pkg.module_metadata_fingerprint =
            std::to_string(std::hash<std::string>{}(pkg.name + "|" +
                                                    pkg.version + "|path"));
        GUARD(append_package(graph, selected, std::move(pkg)));
        return util::Ok;
    }

    if (spec.source == core::DependencySource::Git) {
        if (!allow_local_sources) {
            return std::unexpected(util::make_error(std::format(
                "Resolver Error: registry dependency '{}' cannot use a git source.",
                name)));
        }
        package::ResolvedPackage pkg;
        pkg.name = std::string(name);
        pkg.version = spec.rev.empty() ? "HEAD" : spec.rev;
        pkg.source = "git+" + spec.git + "#" + pkg.version;
        pkg.active_features = sorted_unique_strings(std::move(active_features));
        pkg.module_support = package::ModuleSupport::Named;
        pkg.module_metadata_fingerprint =
            std::to_string(std::hash<std::string>{}(pkg.name + "|" +
                                                    pkg.version + "|git"));
        GUARD(append_package(graph, selected, std::move(pkg)));
        return util::Ok;
    }

    const auto registry_name =
        spec.registry.empty() ? std::string(default_registry) : spec.registry;
    auto resolved = GUARD(registry::reference::resolve_registry_package(
        manifest, registry_name, name, spec.version));
    resolved.module_support = package::ModuleSupport::Named;
    resolved.active_features = sorted_unique_strings(std::move(active_features));
    resolved.module_metadata_fingerprint =
        std::to_string(std::hash<std::string>{}(
            resolved.name + "|" + resolved.version + "|ppargo"));

    const auto added = GUARD(append_package(graph, selected, std::move(resolved)));
    if (!added) {
        return util::Ok;
    }

    const auto dependency_specs = graph.packages.back().dependency_specs;
    for (const auto& dependency : dependency_specs) {
        if (dependency.spec.optional) {
            continue;
        }
        const auto dependency_features =
            features_for_dependency(dependency.spec, {});
        GUARD(resolve_dependency(project_root, manifest, graph, selected,
                                 dependency.name, dependency.spec,
                                 registry_name, dependency_features, false));
    }
    return util::Ok;
}

}  // namespace

namespace package::backends::ppargo_backend {

auto resolve(const std::filesystem::path& project_root,
             const core::Manifest& manifest,
             const FeatureOptions& features) -> util::Result<ResolvedGraph> {
    const auto feature_resolution = GUARD(resolve_root_features(manifest, features));
    ResolvedGraph graph;
    graph.packages.reserve(manifest.dependencies.size());
    std::unordered_map<std::string, std::string> selected_packages;
    const auto default_registry = selected_registry(manifest);

    for (const auto& [name, spec] : manifest.dependencies) {
        if (spec.optional &&
            !feature_resolution.enabled_optional_dependencies.contains(name)) {
            continue;
        }
        const auto forwarded_features =
            feature_resolution.dependency_features.contains(name)
                ? feature_resolution.dependency_features.at(name)
                : std::vector<std::string>{};
        const auto active_features =
            features_for_dependency(spec, forwarded_features);
        GUARD(resolve_dependency(project_root, manifest, graph,
                                 selected_packages, name, spec,
                                 default_registry, active_features, true));
    }

    return graph;
}

auto fetch(const std::filesystem::path& project_root,
           const core::Manifest& manifest, const ResolvedGraph& graph)
    -> util::Status {
    (void)project_root;
    (void)manifest;

    for (const auto& package : graph.packages) {
        if (package.source.rfind("registry+", 0) == 0) {
            GUARD(
                registry::reference::fetch_registry_package(manifest, package));
        } else if (package.source.rfind("git+", 0) == 0) {
            GUARD(ensure_git_checkout(package));
        }
    }

    return util::Ok;
}

auto install(const std::filesystem::path& project_root,
             const core::Manifest& manifest, const ResolvedGraph& graph)
    -> util::Status {
    (void)manifest;

    for (const auto& package : graph.packages) {
        GUARD(install_package_headers(project_root, package));
    }

    return util::Ok;
}

auto publish(const std::filesystem::path& project_root,
             const core::Manifest& manifest) -> util::Status {
    return registry::reference::publish_package(project_root, manifest,
                                                selected_registry(manifest));
}

auto yank(const std::filesystem::path& project_root,
          const core::Manifest& manifest, const YankRequest& request)
    -> util::Status {
    (void)project_root;
    return registry::reference::yank_package(
        manifest, selected_registry(manifest), manifest.package.name,
        request.version, request.undo);
}

auto owner_add(const std::filesystem::path& project_root,
               const core::Manifest& manifest, std::string_view owner)
    -> util::Status {
    (void)project_root;
    return registry::reference::add_owner(manifest, selected_registry(manifest),
                                          manifest.package.name, owner);
}

auto owner_remove(const std::filesystem::path& project_root,
                  const core::Manifest& manifest, std::string_view owner)
    -> util::Status {
    (void)project_root;
    return registry::reference::remove_owner(
        manifest, selected_registry(manifest), manifest.package.name, owner);
}

auto auth_login(const std::filesystem::path& project_root,
                const core::Manifest& manifest, std::string_view registry,
                std::string_view token) -> util::Status {
    (void)project_root;
    (void)manifest;
    const std::string target_registry =
        registry.empty() ? selected_registry(manifest) : std::string(registry);
    return registry::reference::login(target_registry, token);
}

auto auth_logout(const std::filesystem::path& project_root,
                 const core::Manifest& manifest, std::string_view registry)
    -> util::Status {
    (void)project_root;
    (void)manifest;
    const std::string target_registry =
        registry.empty() ? selected_registry(manifest) : std::string(registry);
    return registry::reference::logout(target_registry);
}

}  // namespace package::backends::ppargo_backend
