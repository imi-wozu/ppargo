#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "build/action_scheduler.hpp"
#include "build/compile_profile.hpp"
#include "build/settings.hpp"
#include "core/manifest.hpp"
#include "util/result.hpp"

namespace build::compile {

enum class CompileMode { Build, Check };
enum class OptimizationMode { Normal, Aggressive };

struct JobDecision {
    int jobs = 1;
    std::string reason;
};

struct CompilerConfig {
    std::filesystem::path compiler;
    std::vector<std::string> flags;
    std::vector<std::filesystem::path> include_paths;
    bool modules_enabled = false;
    std::vector<std::string> module_interface_exts;
    std::filesystem::path module_output_dir;
    CompileMode mode = CompileMode::Build;
    OptimizationMode optimization_mode = OptimizationMode::Normal;
    std::string optimization_reason;
    settings::ResolvedBuildSettings build_settings;
    std::optional<std::filesystem::path> pch_file;
    scheduler_runtime::CapacityPlan capacity_plan;
    int jobs = 1;
    std::string jobs_reason;
};

struct CompileResult {
    std::vector<std::filesystem::path> objects;
    std::size_t compiled_count = 0;
};

auto make_compiler_config(const std::filesystem::path& root,
                          const core::Manifest& manifest, bool release,
                          bool exceptions_enabled, CompileMode mode,
                          std::size_t task_count,
                          const std::optional<std::filesystem::path>&
                              output_dir_override,
                          std::span<const std::filesystem::path> dependency_include_paths)
    -> util::Result<CompilerConfig>;

auto make_capacity_plan(CompileMode mode, std::size_t ready_task_count,
                        std::optional<int> forced_jobs,
                        const compile_profile::CompileProfile& profile)
    -> scheduler_runtime::CapacityPlan;

auto compile_signature(const CompilerConfig& config) -> std::string;

auto compile_objects(const std::filesystem::path& source_root,
                     const std::filesystem::path& obj_root,
                     std::span<const std::filesystem::path> sources,
                     const CompilerConfig& config, bool force_rebuild)
    -> util::Result<CompileResult>;

auto run_checks_with_cache(const std::filesystem::path& root,
                           const std::filesystem::path& build_root,
                           std::span<const std::filesystem::path> sources,
                           const CompilerConfig& config) -> util::Status;

}  // namespace build::compile
