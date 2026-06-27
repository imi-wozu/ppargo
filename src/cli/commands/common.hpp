#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "build/targets.hpp"
#include "core/dependency_artifacts.hpp"
#include "core/manifest.hpp"
#include "package/manager.hpp"
#include "util/environment.hpp"

namespace cli::commands::common {

struct CompileEnvironmentSettings {
    std::optional<int> jobs;
    int verbose = 0;
};

struct LoadedProjectContext {
    core::ProjectContext project;
    std::filesystem::path manifest_path;
    std::optional<std::filesystem::path> output_dir_override;
    std::filesystem::path effective_output_dir;
    std::filesystem::path effective_module_output_dir;
};

struct PreparedBuildCommandContext {
    LoadedProjectContext project;
    package::PreparedDependencies dependencies;
};

struct PackageSelectionOptions {
    bool workspace = false;
    bool all = false;
    std::vector<std::string> packages;
    std::vector<std::string> excludes;
};

class ScopedCompileEnvironment {
  public:
    explicit ScopedCompileEnvironment(const CompileEnvironmentSettings& settings);
    ~ScopedCompileEnvironment();

    ScopedCompileEnvironment(const ScopedCompileEnvironment&) = delete;
    auto operator=(const ScopedCompileEnvironment&)
        -> ScopedCompileEnvironment& = delete;
    ScopedCompileEnvironment(ScopedCompileEnvironment&&) = delete;
    auto operator=(ScopedCompileEnvironment&&) -> ScopedCompileEnvironment& =
        delete;

  private:
    std::optional<util::env::ScopedOverride> compile_jobs_;
    std::optional<util::env::ScopedOverride> link_jobs_;
    std::optional<util::env::ScopedOverride> trace_;
    std::optional<util::env::ScopedOverride> trace_incremental_;
};

auto compile_environment_settings(std::optional<int> jobs, int verbose)
    -> CompileEnvironmentSettings;

auto make_target_selection(const std::optional<std::string>& bin,
                           const std::optional<std::string>& example)
    -> std::optional<build::targets::Selection>;

auto load_project_context(const std::optional<std::string>& manifest_path)
    -> util::Result<LoadedProjectContext>;
auto load_project_context(const std::optional<std::string>& manifest_path,
                          const std::optional<std::string>& target_dir)
    -> util::Result<LoadedProjectContext>;
auto apply_target_dir_override(LoadedProjectContext context,
                               const std::optional<std::string>& target_dir)
    -> LoadedProjectContext;
auto make_dependency_workflow_options(
    bool locked, bool offline, bool frozen,
    const std::vector<std::string>& features = {},
    bool all_features = false,
    bool no_default_features = false)
    -> package::DependencyWorkflowOptions;
auto prepare_build_command_context(
    const std::optional<std::string>& manifest_path,
    const std::optional<std::string>& target_dir,
    const package::DependencyWorkflowOptions& workflow_options)
    -> util::Result<PreparedBuildCommandContext>;
auto prepare_build_command_contexts(
    const std::optional<std::string>& manifest_path,
    const std::optional<std::string>& target_dir,
    const package::DependencyWorkflowOptions& workflow_options,
    const PackageSelectionOptions& package_selection)
    -> util::Result<std::vector<PreparedBuildCommandContext>>;
auto selection_uses_dev_dependencies(const build::targets::Selection& selection)
    -> bool;
auto dependency_artifacts_for_selection(
    const PreparedBuildCommandContext& context,
    const build::targets::Selection& selection)
    -> util::Result<core::DependencyArtifacts>;
auto log_selected_targets(const LoadedProjectContext& context, int verbose,
                          std::string_view action,
                          std::span<const build::targets::Selection> selections)
    -> void;
auto selection_label(const build::targets::Selection& selection)
    -> std::string;

auto profile_name(bool release) -> std::string_view;
auto profile_descriptor(bool release) -> std::string_view;

auto elapsed_seconds(std::chrono::steady_clock::time_point start) -> double;

}  // namespace cli::commands::common
