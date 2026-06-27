#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>
#include <span>

#include "util/process.hpp"
#include "util/result.hpp"

namespace build::process_bridge {

enum class CapturePolicy {
    Always,
    FastNoCapture,
    BufferedDiagnostics,
};

struct RunOptions {
    const std::atomic_bool* cancel_requested = nullptr;
    CapturePolicy capture_policy = CapturePolicy::Always;
};

auto run(const std::filesystem::path& program,
         std::span<const std::string>  args,
         const RunOptions& options = {})
    -> util::Result<util::process::RunResult>;

auto run(const std::filesystem::path& program,
         std::span<const std::string>  args,
         const std::atomic_bool* cancel_requested)
    -> util::Result<util::process::RunResult>;

}  // namespace build::process_bridge

