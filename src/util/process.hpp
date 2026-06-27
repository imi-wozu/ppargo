#pragma once

#include <atomic>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "util/result.hpp"

namespace util::process {

enum class StdinMode {
    Inherit,
    Null,
};

struct ProcessMetrics {
    double wall_ms = 0.0;
    std::optional<double> peak_working_set_mb;
    bool used_policy_fallback = false;
};

struct RunOptions {
    std::optional<std::filesystem::path> working_directory;
    bool capture_output = false;
    bool merge_stderr = false;
    StdinMode stdin_mode = StdinMode::Inherit;
    bool collect_metrics = false;
    const std::atomic_bool* cancel_requested = nullptr;
    std::optional<int> timeout_ms;
};

struct RunResult {
    int exit_code = -1;
    std::string output;
    std::optional<ProcessMetrics> metrics;
    bool canceled = false;
};

auto run(const std::filesystem::path& program,
         std::span<const std::string> args, const RunOptions& options = {})
    -> util::Result<RunResult>;
auto run_result(const std::filesystem::path& program,
                std::span<const std::string> args,
                const RunOptions& options = {}) -> util::Result<RunResult>;

}  // namespace util::process
