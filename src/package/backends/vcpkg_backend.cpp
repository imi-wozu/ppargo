#include "package/backends/vcpkg_backend.hpp"
#include "core/paths.hpp"
#include "package/vcpkg.hpp"
#include "util/result.hpp"

#include <format>
#include <regex>
#include <string>
#include <string_view>

namespace {

auto ensure_supported_spec(std::string_view  name,
                           const core::DependencySpec& spec) -> util::Status {
    if (spec.source != core::DependencySource::Registry) {
        return std::unexpected(util::make_error(std::format(
            "Backend Compatibility Error: vcpkg backend does not support '{}' source for dependency '{}'. Use package_manager = \"ppargo\".",
            core::dependency_source_name(spec.source), name)));
    }

    if (!spec.registry.empty() && spec.registry != "vcpkg") {
        return std::unexpected(util::make_error(std::format(
            "Backend Compatibility Error: vcpkg backend cannot use alternate registry '{}' for dependency '{}'.",
            spec.registry, name)));
    }

    if (!spec.features.empty() || spec.optional || !spec.default_features) {
        return std::unexpected(util::make_error(std::format(
            "Backend Compatibility Error: vcpkg backend does not support Cargo feature flags/optional dependency controls for '{}'.",
            name)));
    }

    static const std::regex exact_version(R"(^\d+\.\d+\.\d+(#\d+)?$)");
    if (!std::regex_match(spec.version, exact_version)) {
        return std::unexpected(util::make_error(std::format(
            "Backend Compatibility Error: vcpkg backend requires exact dependency versions (e.g. 1.2.3) for '{}'. Got '{}'.",
            name, spec.version)));
    }

    return util::Ok;
}

auto match_version(std::string_view  requested, std::string_view  resolved) -> bool {
    if (resolved == requested) {
        return true;
    }
    return resolved.rfind(std::string(requested) + "#", 0) == 0;
}

auto package_name_matches(std::string_view  requested, std::string_view  candidate)
    -> bool {
    if (candidate == requested) {
        return true;
    }
    return candidate.rfind(std::string(requested) + ":", 0) == 0;
}

auto ensure_supported_feature_options(const package::FeatureOptions& features)
    -> util::Status {
    if (features.requested.empty() && !features.all_features &&
        !features.no_default_features) {
        return util::Ok;
    }
    return std::unexpected(util::make_error(
        "Backend Compatibility Error: vcpkg backend does not support Cargo "
        "feature flags. Use package_manager = \"ppargo\"."));
}

}  // namespace

namespace package::backends::vcpkg_backend {

auto resolve(const std::filesystem::path& project_root,
             const core::Manifest& manifest,
             const FeatureOptions& features) -> util::Result<ResolvedGraph> {
    (void)project_root;
    GUARD(ensure_supported_feature_options(features));

    ResolvedGraph graph;
    graph.packages.reserve(manifest.dependencies.size());

    for (const auto& [name, spec] : manifest.dependencies) {
        GUARD(ensure_supported_spec(name, spec));

        auto matches_result = vcpkg::search_packages(manifest, name);
        if (!matches_result) {
            return std::unexpected(matches_result.error());
        }
        const auto matches = *matches_result;
        if (matches.empty()) {
            return std::unexpected(util::make_error(std::format(
                "Package '{}' not found in the vcpkg registry.", name)));
        }

        auto selected = matches.front();
        bool found = false;
        for (const auto& candidate : matches) {
            if (package_name_matches(name, candidate.name) &&
                match_version(spec.version, candidate.version)) {
                selected = candidate;
                found = true;
                break;
            }
        }

        if (!found) {
            return std::unexpected(util::make_error(std::format(
                "Backend Compatibility Error: vcpkg could not resolve exact requested version '{}' for '{}'.",
                spec.version, name)));
        }

        ResolvedPackage pkg;
        pkg.name = selected.name;
        pkg.version = selected.version;
        pkg.source = "vcpkg";
        pkg.checksum.clear();
        pkg.artifact.clear();
        pkg.module_support = ModuleSupport::Named;
        pkg.module_metadata_fingerprint =
            std::to_string(std::hash<std::string>{}(pkg.name + "|" + pkg.version + "|vcpkg"));
        graph.packages.push_back(std::move(pkg));
    }

    return graph;
}

auto fetch(const std::filesystem::path& project_root,
           const core::Manifest& manifest,
           const ResolvedGraph& graph) -> util::Status {
    (void)project_root;
    (void)manifest;
    (void)graph;
    return util::Ok;
}

auto install(const std::filesystem::path& project_root,
             const core::Manifest& manifest,
             const ResolvedGraph& graph) -> util::Status {
    GUARD(vcpkg::ensure_vcpkg_manifest(project_root));
    GUARD(vcpkg::sync_dependencies(project_root, graph));
    return vcpkg::install_dependencies(project_root, manifest, graph);
}

auto publish(const std::filesystem::path& project_root,
             const core::Manifest& manifest) -> util::Status {
    (void)project_root;
    (void)manifest;
    return std::unexpected(util::make_error("Backend Compatibility Error: vcpkg backend does not support `argo publish`. Use package_manager = \"ppargo\"."));
}

auto yank(const std::filesystem::path& project_root,
          const core::Manifest& manifest,
          const YankRequest& request) -> util::Status {
    (void)project_root;
    (void)manifest;
    (void)request;
    return std::unexpected(util::make_error("Backend Compatibility Error: vcpkg backend does not support `argo yank`. Use package_manager = \"ppargo\"."));
}

auto owner_add(const std::filesystem::path& project_root,
               const core::Manifest& manifest,
               std::string_view  owner) -> util::Status {
    (void)project_root;
    (void)manifest;
    (void)owner;
    return std::unexpected(util::make_error("Backend Compatibility Error: vcpkg backend does not support `argo owner`. Use package_manager = \"ppargo\"."));
}

auto owner_remove(const std::filesystem::path& project_root,
                  const core::Manifest& manifest,
                  std::string_view  owner) -> util::Status {
    (void)project_root;
    (void)manifest;
    (void)owner;
    return std::unexpected(util::make_error("Backend Compatibility Error: vcpkg backend does not support `argo owner`. Use package_manager = \"ppargo\"."));
}

auto auth_login(const std::filesystem::path& project_root,
                const core::Manifest& manifest,
                std::string_view  registry,
                std::string_view  token) -> util::Status {
    (void)project_root;
    (void)manifest;
    (void)registry;
    (void)token;
    return std::unexpected(util::make_error("Backend Compatibility Error: vcpkg backend does not support `argo login`. Use package_manager = \"ppargo\"."));
}

auto auth_logout(const std::filesystem::path& project_root,
                 const core::Manifest& manifest,
                 std::string_view  registry) -> util::Status {
    (void)project_root;
    (void)manifest;
    (void)registry;
    return std::unexpected(util::make_error("Backend Compatibility Error: vcpkg backend does not support `argo logout`. Use package_manager = \"ppargo\"."));
}

}  // namespace package::backends::vcpkg_backend



