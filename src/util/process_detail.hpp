#pragma once

#include <chrono>
#include <filesystem>
#include <span>
#include <string>

#include "util/process.hpp"
#include "util/result.hpp"

namespace util::process::detail {

auto cancellation_requested(const RunOptions& options) -> bool;
auto timeout_expired(std::chrono::steady_clock::time_point wall_start,
                     const RunOptions& options) -> bool;
auto run_platform_process(const std::filesystem::path& program,
                          std::span<const std::string> args,
                          const RunOptions& options,
                          bool allow_policy_runner_copy,
                          bool policy_fallback_used)
    -> util::Result<RunResult>;

}  // namespace util::process::detail
