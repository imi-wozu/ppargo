#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "core/dependency_artifacts.hpp"
#include "core/manifest.hpp"
#include "package/backend.hpp"
#include "util/result.hpp"

namespace package {

struct DependencyWorkflowOptions {
    bool locked = false;
    bool offline = false;
    FeatureOptions features;
};

struct PreparedDependencies {
    ResolvedGraph graph;
};

struct BuildArtifactOptions {
    bool include_dev_dependencies = false;
};

auto prepare_dependencies(
    const core::ProjectContext& context,
    const DependencyWorkflowOptions& options = {})
    -> util::Result<PreparedDependencies>;
auto build_dependency_artifacts(const core::ProjectContext& context,
                                const PreparedDependencies& prepared,
                                const BuildArtifactOptions& options = {})
    -> util::Result<core::DependencyArtifacts>;
auto fetch_dependencies(const core::ProjectContext& context) -> util::Status;
auto add_dependency(const std::filesystem::path& project_root,
                    std::string_view dep_spec) -> util::Status;
auto remove_dependency(const std::filesystem::path& project_root,
                       std::string_view dep_name) -> util::Status;
auto update_dependencies(const std::filesystem::path& project_root,
                         const std::optional<std::string>& dep_name)
    -> util::Status;

auto publish_package(const std::filesystem::path& project_root) -> util::Status;
auto yank_package(const std::filesystem::path& project_root,
                  std::string_view version, bool undo) -> util::Status;
auto add_owner(const std::filesystem::path& project_root,
               std::string_view owner) -> util::Status;
auto remove_owner(const std::filesystem::path& project_root,
                  std::string_view owner) -> util::Status;
auto auth_login(const std::filesystem::path& project_root,
                std::string_view registry, std::string_view token)
    -> util::Status;
auto auth_logout(const std::filesystem::path& project_root,
                 std::string_view registry) -> util::Status;

}  // namespace package
