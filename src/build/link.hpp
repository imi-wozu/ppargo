#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>
#include <span>

#include "build/compile.hpp"
#include "util/result.hpp"

namespace build::link {

struct LinkExecutionResult {
    int exit_code = 0;
    std::string output;
    bool canceled = false;
};

auto ensure_not_running_target(const std::filesystem::path& output) -> util::Status;

auto link_binary_result(
    const compile::CompilerConfig& config,
    std::span<const std::filesystem::path>  object_files,
    const std::filesystem::path& output,
    std::span<const std::filesystem::path>  library_paths,
    std::span<const std::string>  libraries, bool release,
    const std::atomic_bool* cancel_requested = nullptr)
    -> util::Result<LinkExecutionResult>;

auto copy_runtime_dlls(std::span<const std::filesystem::path> runtime_files,
                       const std::filesystem::path& build_dir) -> util::Status;

}  // namespace build::link
