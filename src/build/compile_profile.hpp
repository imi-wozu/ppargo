#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "util/result.hpp"

namespace build::compile_profile {

struct TaskStats {
    double ewma_ms = 0.0;
    double ewma_peak_mb = 0.0;
    double ewma_startup_ms = 0.0;
    std::uint32_t samples = 0;
};

struct CompileProfile {
    std::unordered_map<std::string, TaskStats> stats;
};

auto profile_path_for_build_root(const std::filesystem::path& build_root)
    -> std::filesystem::path;

auto make_task_key(const std::filesystem::path& source,
                   std::string_view signature_hash,
                   std::string_view mode_tag) -> std::string;

auto load_profile(const std::filesystem::path& build_root)
    -> util::Result<CompileProfile>;
auto save_profile(const std::filesystem::path& build_root,
                  const CompileProfile& profile) -> util::Status;

auto lookup_stats(const CompileProfile& profile, std::string_view key)
    -> std::optional<TaskStats>;
auto lookup_cost_ms(const CompileProfile& profile, std::string_view key)
    -> std::optional<double>;
auto lookup_peak_mb(const CompileProfile& profile, std::string_view key)
    -> std::optional<double>;
auto update_task_stats(CompileProfile& profile, std::string_view key,
                       double sample_ms,
                       std::optional<double> peak_mb = std::nullopt,
                       double startup_ms = 0.0) -> void;

}  // namespace build::compile_profile
