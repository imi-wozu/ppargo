#include "cli/commands/common.hpp"

#include <algorithm>
#include <format>
#include <set>
#include <utility>

#include "core/paths.hpp"
#include "util/output.hpp"

namespace cli::commands::common {

auto compile_environment_settings(std::optional<int> jobs, int verbose)
    -> CompileEnvironmentSettings {
    return CompileEnvironmentSettings{
        .jobs = jobs,
        .verbose = verbose,
    };
}

ScopedCompileEnvironment::ScopedCompileEnvironment(
    const CompileEnvironmentSettings& settings) {
    if (settings.jobs.has_value()) {
        const auto jobs = std::to_string(*settings.jobs);
        compile_jobs_.emplace("PPARGO_FORCE_COMPILE_JOBS", jobs);
        link_jobs_.emplace("PPARGO_FORCE_LINK_JOBS", jobs);
    }
    if (settings.verbose >= 2) {
        trace_.emplace("PPARGO_TRACE", "1");
        trace_incremental_.emplace("PPARGO_TRACE_INCREMENTAL", "1");
    }
}

ScopedCompileEnvironment::~ScopedCompileEnvironment() = default;

auto make_target_selection(const std::optional<std::string>& bin,
                           const std::optional<std::string>& example)
    -> std::optional<build::targets::Selection> {
    if (bin.has_value()) {
        return build::targets::Selection{
            .kind = build::targets::SelectionKind::Bin,
            .name = *bin,
        };
    }
    if (example.has_value()) {
        return build::targets::Selection{
            .kind = build::targets::SelectionKind::Example,
            .name = *example,
        };
    }
    return std::nullopt;
}

auto load_project_context(const std::optional<std::string>& manifest_path)
    -> util::Result<LoadedProjectContext> {
    return load_project_context(manifest_path, std::nullopt);
}

auto load_project_context(const std::optional<std::string>& manifest_path,
                          const std::optional<std::string>& target_dir)
    -> util::Result<LoadedProjectContext> {
    LoadedProjectContext context;
    if (manifest_path.has_value()) {
        context.project =
            GUARD(core::load_project_context_from_manifest(*manifest_path));
        context.manifest_path = std::filesystem::path(*manifest_path);
    } else {
        context.project = GUARD(core::load_project_context());
        context.manifest_path = context.project.root / "ppargo.toml";
    }
    if (target_dir.has_value()) {
        context.output_dir_override = std::filesystem::path(*target_dir);
    }
    context.effective_output_dir = core::effective_output_dir(
        context.project.manifest, context.output_dir_override);
    context.effective_module_output_dir = core::effective_module_output_dir(
        context.project.manifest, context.output_dir_override);
    return context;
}

auto apply_target_dir_override(LoadedProjectContext context,
                               const std::optional<std::string>& target_dir)
    -> LoadedProjectContext {
    if (target_dir.has_value()) {
        context.output_dir_override = std::filesystem::path(*target_dir);
    } else {
        context.output_dir_override.reset();
    }
    context.effective_output_dir = core::effective_output_dir(
        context.project.manifest, context.output_dir_override);
    context.effective_module_output_dir = core::effective_module_output_dir(
        context.project.manifest, context.output_dir_override);
    return context;
}

auto make_dependency_workflow_options(
    bool locked, bool offline, bool frozen,
    const std::vector<std::string>& features,
    bool all_features,
    bool no_default_features)
    -> package::DependencyWorkflowOptions {
    return package::DependencyWorkflowOptions{
        .locked = locked || frozen,
        .offline = offline || frozen,
        .features = package::FeatureOptions{
            .requested = features,
            .all_features = all_features,
            .no_default_features = no_default_features,
        },
    };
}

namespace {

auto uses_workspace_selection(const PackageSelectionOptions& options) -> bool {
    return options.workspace || options.all || !options.packages.empty() ||
           !options.excludes.empty();
}

auto absolute_normal(const std::filesystem::path& path)
    -> std::filesystem::path {
    std::error_code ec;
    auto value = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return value;
    }
    value = std::filesystem::absolute(path, ec);
    if (!ec) {
        return value.lexically_normal();
    }
    return path.lexically_normal();
}

auto find_workspace_manifest(const std::filesystem::path& start)
    -> util::Result<std::filesystem::path> {
    std::error_code ec;
    auto current = std::filesystem::absolute(start, ec);
    if (ec) {
        return std::unexpected(util::make_error(
            "Failed to resolve workspace search path: " + start.string() +
            " (" + ec.message() + ")"));
    }
    if (!std::filesystem::is_directory(current, ec)) {
        current = current.parent_path();
    }

    while (true) {
        const auto candidate = current / "ppargo.toml";
        if (std::filesystem::exists(candidate, ec) && !ec) {
            auto manifest = GUARD(core::load_manifest(candidate));
            if (manifest.workspace.enabled) {
                return candidate;
            }
        }
        if (ec) {
            return std::unexpected(util::make_error(
                "Failed while searching for workspace manifest: " +
                ec.message()));
        }
        if (!current.has_parent_path() || current.parent_path() == current) {
            return std::unexpected(util::make_error(
                "Could not find a [workspace] manifest in the current "
                "directory or any parent."));
        }
        current = current.parent_path();
    }
}

auto wildcard_match(std::string_view pattern, std::string_view value) -> bool {
    const auto star = pattern.find('*');
    if (star == std::string_view::npos) {
        return pattern == value;
    }
    const auto prefix = pattern.substr(0, star);
    const auto suffix = pattern.substr(star + 1);
    return value.size() >= prefix.size() + suffix.size() &&
           value.starts_with(prefix) && value.ends_with(suffix);
}

auto expand_member_pattern(const std::filesystem::path& workspace_root,
                           const std::filesystem::path& pattern)
    -> util::Result<std::vector<std::filesystem::path>> {
    const auto pattern_text = pattern.generic_string();
    if (pattern_text.find('*') == std::string::npos) {
        return std::vector<std::filesystem::path>{workspace_root / pattern};
    }

    const auto parent_pattern = pattern.parent_path();
    const auto leaf_pattern = pattern.filename().generic_string();
    if (leaf_pattern.find('*') == std::string::npos) {
        return std::unexpected(util::make_error(
            "Workspace member wildcards are only supported in the final path "
            "component."));
    }

    const auto parent = workspace_root / parent_pattern;
    std::error_code ec;
    if (!std::filesystem::exists(parent, ec) || ec) {
        if (ec) {
            return std::unexpected(util::make_error(
                "Failed to inspect workspace member parent: " +
                parent.string() + " (" + ec.message() + ")"));
        }
        return std::vector<std::filesystem::path>{};
    }

    std::vector<std::filesystem::path> members;
    for (std::filesystem::directory_iterator it(parent, ec), end; it != end;
         it.increment(ec)) {
        if (ec) {
            return std::unexpected(util::make_error(
                "Failed to enumerate workspace members: " + ec.message()));
        }
        if (!it->is_directory(ec) || ec) {
            ec.clear();
            continue;
        }
        if (wildcard_match(leaf_pattern, it->path().filename().generic_string())) {
            members.push_back(it->path());
        }
    }
    std::sort(members.begin(), members.end());
    return members;
}

auto member_matches_path_pattern(const std::filesystem::path& workspace_root,
                                 const std::filesystem::path& member_root,
                                 const std::filesystem::path& pattern) -> bool {
    const auto relative =
        absolute_normal(member_root).lexically_relative(absolute_normal(workspace_root));
    return wildcard_match(pattern.generic_string(), relative.generic_string());
}

auto workspace_search_start(const std::optional<std::string>& manifest_path)
    -> util::Result<std::filesystem::path> {
    if (manifest_path.has_value()) {
        std::error_code ec;
        auto absolute_manifest =
            std::filesystem::absolute(std::filesystem::path(*manifest_path), ec);
        if (ec) {
            return std::unexpected(util::make_error(
                "Failed to resolve manifest path: " + *manifest_path + " (" +
                ec.message() + ")"));
        }
        return absolute_manifest.parent_path();
    }

    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    if (ec) {
        return std::unexpected(
            util::make_error("Failed to read current directory: " + ec.message()));
    }
    return cwd;
}

}  // namespace

auto prepare_build_command_context(
    const std::optional<std::string>& manifest_path,
    const std::optional<std::string>& target_dir,
    const package::DependencyWorkflowOptions& workflow_options)
    -> util::Result<PreparedBuildCommandContext> {
    auto context = GUARD(load_project_context(manifest_path, target_dir));
    if (!context.project.manifest.package_defined &&
        context.project.manifest.workspace.enabled) {
        return std::unexpected(util::make_error(
            "The selected manifest is a virtual workspace. Use --workspace or "
            "-p/--package to select workspace members."));
    }
    auto dependencies =
        GUARD(package::prepare_dependencies(context.project, workflow_options));
    return PreparedBuildCommandContext{
        .project = std::move(context),
        .dependencies = std::move(dependencies),
    };
}

auto prepare_build_command_contexts(
    const std::optional<std::string>& manifest_path,
    const std::optional<std::string>& target_dir,
    const package::DependencyWorkflowOptions& workflow_options,
    const PackageSelectionOptions& package_selection)
    -> util::Result<std::vector<PreparedBuildCommandContext>> {
    if (!uses_workspace_selection(package_selection)) {
        std::vector<PreparedBuildCommandContext> contexts;
        contexts.push_back(GUARD(prepare_build_command_context(
            manifest_path, target_dir, workflow_options)));
        return contexts;
    }

    const auto start = GUARD(workspace_search_start(manifest_path));
    const auto workspace_manifest_path = GUARD(find_workspace_manifest(start));
    const auto workspace_root = workspace_manifest_path.parent_path();
    const auto workspace_manifest =
        GUARD(core::load_manifest(workspace_manifest_path));

    std::vector<std::filesystem::path> member_roots;
    if (workspace_manifest.package_defined) {
        member_roots.push_back(workspace_root);
    }
    for (const auto& pattern : workspace_manifest.workspace.members) {
        auto expanded = GUARD(expand_member_pattern(workspace_root, pattern));
        member_roots.insert(member_roots.end(), expanded.begin(), expanded.end());
    }

    std::set<std::filesystem::path> seen;
    std::vector<PreparedBuildCommandContext> contexts;
    for (const auto& raw_member_root : member_roots) {
        const auto member_root = absolute_normal(raw_member_root);
        if (!seen.insert(member_root).second) {
            continue;
        }

        bool excluded_by_manifest = false;
        for (const auto& exclude : workspace_manifest.workspace.exclude) {
            if (member_matches_path_pattern(workspace_root, member_root, exclude)) {
                excluded_by_manifest = true;
                break;
            }
        }
        if (excluded_by_manifest) {
            continue;
        }

        const auto member_manifest_path = member_root / "ppargo.toml";
        std::error_code ec;
        if (!std::filesystem::exists(member_manifest_path, ec) || ec) {
            if (ec) {
                return std::unexpected(util::make_error(
                    "Failed to inspect workspace member manifest: " +
                    member_manifest_path.string() + " (" + ec.message() + ")"));
            }
            continue;
        }

        auto member_context = GUARD(load_project_context(
            member_manifest_path.string(), target_dir));
        if (!member_context.project.manifest.package_defined) {
            return std::unexpected(util::make_error(
                "Workspace member is missing [package]: " +
                member_manifest_path.string()));
        }

        const auto& package_name = member_context.project.manifest.package.name;
        if (!package_selection.packages.empty() &&
            std::ranges::find(package_selection.packages, package_name) ==
                package_selection.packages.end()) {
            continue;
        }
        if (std::ranges::find(package_selection.excludes, package_name) !=
            package_selection.excludes.end()) {
            continue;
        }

        auto dependencies = GUARD(
            package::prepare_dependencies(member_context.project, workflow_options));
        contexts.push_back(PreparedBuildCommandContext{
            .project = std::move(member_context),
            .dependencies = std::move(dependencies),
        });
    }

    if (contexts.empty()) {
        return std::unexpected(util::make_error(
            "Workspace selection did not match any packages."));
    }
    return contexts;
}

auto selection_uses_dev_dependencies(const build::targets::Selection& selection)
    -> bool {
    return selection.kind == build::targets::SelectionKind::UnitTest ||
           selection.kind == build::targets::SelectionKind::IntegrationTest;
}

auto dependency_artifacts_for_selection(
    const PreparedBuildCommandContext& context,
    const build::targets::Selection& selection)
    -> util::Result<core::DependencyArtifacts> {
    return package::build_dependency_artifacts(
        context.project.project, context.dependencies,
        package::BuildArtifactOptions{
            .include_dev_dependencies =
                selection_uses_dev_dependencies(selection),
        });
}

auto log_selected_targets(const LoadedProjectContext& context, int verbose,
                          std::string_view action,
                          std::span<const build::targets::Selection> selections)
    -> void {
    if (verbose <= 0) {
        return;
    }

    util::output::info(
        std::format("manifest: {}", context.manifest_path.string()));
    util::output::info(
        std::format("target dir: {}",
                    (context.project.root / context.effective_output_dir).string()));
    for (const auto& selection : selections) {
        util::output::info(
            std::format("{} {}", action, selection_label(selection)));
    }
}

auto selection_label(const build::targets::Selection& selection)
    -> std::string {
    switch (selection.kind) {
        case build::targets::SelectionKind::Bin:
        case build::targets::SelectionKind::DefaultBin:
            return std::format("bin {}", selection.name);
        case build::targets::SelectionKind::Example:
            return std::format("example {}", selection.name);
        case build::targets::SelectionKind::Bench:
            return std::format("bench {}", selection.name);
        case build::targets::SelectionKind::UnitTest:
            return "unit tests";
        case build::targets::SelectionKind::IntegrationTest:
            return std::format("integration test {}", selection.name);
    }
    return selection.name;
}

auto profile_name(bool release) -> std::string_view {
    return release ? "release" : "dev";
}

auto profile_descriptor(bool release) -> std::string_view {
    return release ? "[optimized]" : "[unoptimized + debuginfo]";
}

auto elapsed_seconds(std::chrono::steady_clock::time_point start) -> double {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::duration<double>>(
            std::chrono::steady_clock::now() - start);
    return elapsed.count();
}

}  // namespace cli::commands::common
