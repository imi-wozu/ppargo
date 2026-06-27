#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <span>

#include "build/compile.hpp"
#include "util/process.hpp"

namespace build::compile::detail {

auto check_source(const CompilerConfig& config, const std::filesystem::path& source,
                  const std::atomic_bool* cancel_requested = nullptr)
    -> util::Result<util::process::ProcessMetrics>;

auto run_check_batch(const CompilerConfig& config,
                     std::span<const std::filesystem::path> sources,
                     const std::atomic_bool* cancel_requested = nullptr)
    -> util::Result<util::process::ProcessMetrics>;

}  // namespace build::compile::detail
