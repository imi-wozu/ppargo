#include "package/manager.hpp"

#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "package/backend.hpp"
#include "package/home.hpp"
#include "package/manager_lock.hpp"
#include "package/manager_mutation.hpp"
#include "package/manager_workflow.hpp"
#include "package/vcpkg.hpp"
#include "core/paths.hpp"
#include "util/containers.hpp"
#include "util/result.hpp"

namespace package {

namespace {

auto offline_state_ready(const std::filesystem::path& project_root,
                         const core::Manifest& manifest,
                         const package::ResolvedGraph& graph) -> util::Status {
    const auto backend = GUARD(package::backend_name(manifest));
    if (backend == "vcpkg") {
        const auto installed =
            GUARD(package::vcpkg::required_packages_installed(project_root, graph));
        if (!installed) {
            return std::unexpected(util::make_error(
                "Offline build requires dependencies to already be installed locally for the vcpkg backend."));
        }
        return util::Ok;
    }

    const auto include_root =
        project_root / "packages" / core::detect_triplet() / "include";
    for (const auto& package : graph.packages) {
        if (package.source.rfind("path+", 0) == 0) {
            continue;
        }
        std::error_code ec;
        const auto package_include = include_root / package.name;
        if (std::filesystem::exists(package_include, ec) && !ec) {
            continue;
        }

        if (package.source.rfind("registry+", 0) == 0) {
            const auto src_dir = GUARD(package::home::registry_src_dir());
            if (std::filesystem::exists(src_dir / (package.name + "-" + package.version),
                                        ec) &&
                !ec) {
                continue;
            }
        }

        if (package.source.rfind("git+", 0) == 0) {
            const auto hash =
                std::to_string(std::hash<std::string>{}(package.source));
            const auto checkouts = GUARD(package::home::git_checkout_dir());
            if (std::filesystem::exists(checkouts / (package.name + "-" + hash), ec) &&
                !ec) {
                continue;
            }
        }

        if (ec) {
            return std::unexpected(util::make_error(std::format(
                "Failed to inspect offline dependency state for '{}' ({})",
                package.name, ec.message())));
        }
        return std::unexpected(util::make_error(std::format(
            "Offline build requires dependency '{}' to already be fetched or installed locally.",
            package.name)));
    }
    return util::Ok;
}

auto triplet_install_root(const std::filesystem::path& project_root)
    -> std::filesystem::path {
    return project_root / "packages" / core::detect_triplet();
}

auto append_registry_link_libraries(const core::DependencyMap& dependencies,
                                    std::vector<std::string>& libraries) -> void {
    for (const auto& [name, spec] : dependencies) {
        if (spec.source == core::DependencySource::Registry) {
            libraries.push_back(name);
        }
    }
}

auto append_runtime_files(const std::filesystem::path& runtime_dir,
                          std::vector<std::filesystem::path>& runtime_files)
    -> util::Status {
    std::error_code ec;
    if (!std::filesystem::exists(runtime_dir, ec) || ec) {
        if (ec) {
            return std::unexpected(util::make_error(
                "Failed to inspect runtime directory: " + runtime_dir.string() +
                " (" + ec.message() + ")"));
        }
        return util::Ok;
    }

    for (std::filesystem::directory_iterator it(runtime_dir, ec), end; it != end;
         it.increment(ec)) {
        if (ec) {
            return std::unexpected(util::make_error(
                "Failed to enumerate runtime directory: " + runtime_dir.string() +
                " (" + ec.message() + ")"));
        }
        if (!it->is_regular_file(ec)) {
            if (ec) {
                return std::unexpected(util::make_error(
                    "Failed to inspect runtime artifact: " +
                    it->path().string() + " (" + ec.message() + ")"));
            }
            continue;
        }
        runtime_files.push_back(it->path());
    }

    return util::Ok;
}

}  // namespace

auto prepare_dependencies(const core::ProjectContext& context,
                          const DependencyWorkflowOptions& options)
    -> util::Result<PreparedDependencies> {
    if (!context.manifest.features.packages) {
        return PreparedDependencies{};
    }

    auto graph = GUARD(detail::resolve_with_lock(
        context.root, context.manifest,
        detail::ResolveOptions{
            .force_refresh = false,
            .locked = options.locked,
            .offline = options.offline,
            .features = options.features,
        }));
    if (context.manifest.build.modules) {
        for (const auto& package : graph.packages) {
            if (package.module_support == package::ModuleSupport::None) {
                return std::unexpected(util::make_error(std::format(
                    "Backend Compatibility Error: dependency '{}' has "
                    "module_support=none. "
                    "This dependency cannot be imported as a module.",
                    package.name)));
            }
        }
    }
    if (options.offline) {
        GUARD(offline_state_ready(context.root, context.manifest, graph));
        const auto backend = GUARD(package::backend_name(context.manifest));
        if (backend == "vcpkg") {
            return PreparedDependencies{.graph = std::move(graph)};
        }
        GUARD(package::install(context.root, context.manifest, graph));
        return PreparedDependencies{.graph = std::move(graph)};
    }
    GUARD(package::fetch(context.root, context.manifest, graph));
    GUARD(package::install(context.root, context.manifest, graph));
    return PreparedDependencies{.graph = std::move(graph)};
}

auto build_dependency_artifacts(const core::ProjectContext& context,
                                const PreparedDependencies& prepared,
                                const BuildArtifactOptions& options)
    -> util::Result<core::DependencyArtifacts> {
    (void)prepared;

    core::DependencyArtifacts artifacts;
    if (!context.manifest.features.packages) {
        return artifacts;
    }

    const auto backend = GUARD(package::backend_name(context.manifest));
    if (backend == "vcpkg" || backend == "ppargo") {
        const auto install_root = triplet_install_root(context.root);
        artifacts.include_directories.push_back(install_root / "include");
        artifacts.library_directories.push_back(install_root / "lib");
#ifdef _WIN32
        const auto runtime_dir = install_root / "bin";
        artifacts.runtime_directories.push_back(runtime_dir);
        GUARD(append_runtime_files(runtime_dir, artifacts.runtime_files));
#endif
    }

    append_registry_link_libraries(context.manifest.dependencies,
                                   artifacts.link_libraries);
    if (options.include_dev_dependencies) {
        append_registry_link_libraries(context.manifest.dev_dependencies,
                                       artifacts.link_libraries);
    }

    util::sort_and_deduplicate(artifacts.include_directories);
    util::sort_and_deduplicate(artifacts.library_directories);
    util::sort_and_deduplicate(artifacts.link_libraries);
    util::sort_and_deduplicate(artifacts.runtime_directories);
    util::sort_and_deduplicate(artifacts.runtime_files);
    return artifacts;
}

auto fetch_dependencies(const core::ProjectContext& context) -> util::Status {
    if (!context.manifest.features.packages) {
        return util::Ok;
    }

    return detail::resolve_and_fetch_only(context.root, context.manifest, false);
}

auto add_dependency(const std::filesystem::path& project_root,
                    std::string_view dep_spec) -> util::Status {
    auto parsed = GUARD(detail::parse_dependency_input(dep_spec));
    auto manifest = GUARD(detail::load_project_manifest(project_root));
    GUARD(detail::require_package_management_enabled(manifest));

    core::DependencySpec spec{};
    spec.source = core::DependencySource::Registry;
    if (parsed.version.has_value()) {
        spec.version = *parsed.version;
    } else {
        auto inferred_version = GUARD(
            detail::infer_version_for_add(project_root, manifest, parsed.name));
        spec.version = std::move(inferred_version);
    }

    manifest.dependencies[parsed.name] = std::move(spec);
    GUARD(detail::save_project_dependencies(project_root, manifest));
    return detail::resolve_fetch_install(project_root, manifest, true);
}

auto remove_dependency(const std::filesystem::path& project_root,
                       std::string_view dep_name) -> util::Status {
    auto manifest = GUARD(detail::load_project_manifest(project_root));
    if (!manifest.dependencies.contains(std::string(dep_name))) {
        return std::unexpected(util::make_error(std::format(
            "Dependency '{}' is not declared in [dependencies].", dep_name)));
    }

    manifest.dependencies.erase(std::string(dep_name));
    GUARD(detail::save_project_dependencies(project_root, manifest));
    return detail::resolve_fetch_install(project_root, manifest, true);
}

auto update_dependencies(const std::filesystem::path& project_root,
                         const std::optional<std::string>& dep_name)
    -> util::Status {
    auto manifest = GUARD(detail::load_project_manifest(project_root));
    if (dep_name.has_value() && !dep_name->empty() &&
        !manifest.dependencies.contains(*dep_name)) {
        return std::unexpected(util::make_error(std::format(
            "Dependency '{}' is not declared in [dependencies].", *dep_name)));
    }

    return detail::resolve_fetch_install(project_root, manifest, true);
}

auto publish_package(const std::filesystem::path& project_root)
    -> util::Status {
    return detail::with_project_manifest(project_root, [&](const auto& manifest) {
        return package::publish(project_root, manifest);
    });
}

auto yank_package(const std::filesystem::path& project_root,
                  std::string_view version, bool undo) -> util::Status {
    return detail::with_project_manifest(project_root, [&](const auto& manifest) {
        return package::yank(
            project_root, manifest,
            package::YankRequest{.version = std::string(version), .undo = undo});
    });
}

auto add_owner(const std::filesystem::path& project_root,
               std::string_view owner) -> util::Status {
    return detail::with_project_manifest(project_root, [&](const auto& manifest) {
        return package::owner_add(project_root, manifest, owner);
    });
}

auto remove_owner(const std::filesystem::path& project_root,
                  std::string_view owner) -> util::Status {
    return detail::with_project_manifest(project_root, [&](const auto& manifest) {
        return package::owner_remove(project_root, manifest, owner);
    });
}

auto auth_login(const std::filesystem::path& project_root,
                std::string_view registry, std::string_view token)
    -> util::Status {
    return detail::with_project_manifest(project_root, [&](const auto& manifest) {
        return package::auth_login(project_root, manifest, registry, token);
    });
}

auto auth_logout(const std::filesystem::path& project_root,
                 std::string_view registry) -> util::Status {
    return detail::with_project_manifest(project_root, [&](const auto& manifest) {
        return package::auth_logout(project_root, manifest, registry);
    });
}

}  // namespace package
