#include "build/check_batches.hpp"

#include <string>
#include <vector>

#include "build/process_bridge.hpp"

namespace build::compile::detail {

auto check_source(const CompilerConfig& config, const std::filesystem::path& source,
                  const std::atomic_bool* cancel_requested)
    -> util::Result<util::process::ProcessMetrics> {
    std::vector<std::string> args;
    args.reserve(config.flags.size() + config.include_paths.size() * 2 + 5);
    for (const auto& flag : config.flags) {
        args.push_back(flag);
    }
    for (const auto& include_path : config.include_paths) {
        args.push_back("-I");
        args.push_back(include_path.string());
    }
    if (config.pch_file.has_value()) {
        args.push_back("-include-pch");
        args.push_back(config.pch_file->string());
    }
    args.push_back("-fsyntax-only");
    args.push_back(source.string());

    auto run = GUARD(build::process_bridge::run(
        config.compiler, args,
        build::process_bridge::RunOptions{
            .cancel_requested = cancel_requested,
            .capture_policy = build::process_bridge::CapturePolicy::BufferedDiagnostics,
        }));
    if (run.canceled) {
        return util::process::ProcessMetrics{};
    }
    if (run.exit_code != 0) {
        return std::unexpected(
            util::make_error("Check failed for: " + source.string()));
    }
    return run.metrics.value_or(util::process::ProcessMetrics{});
}

auto run_check_batch(const CompilerConfig& config,
                     std::span<const std::filesystem::path> sources,
                     const std::atomic_bool* cancel_requested)
    -> util::Result<util::process::ProcessMetrics> {
    if (sources.empty()) {
        return util::process::ProcessMetrics{};
    }

    std::vector<std::string> args;
    args.reserve(config.flags.size() + config.include_paths.size() * 2 +
                 sources.size() + 3);
    for (const auto& flag : config.flags) {
        args.push_back(flag);
    }
    for (const auto& include_path : config.include_paths) {
        args.push_back("-I");
        args.push_back(include_path.string());
    }
    if (config.pch_file.has_value()) {
        args.push_back("-include-pch");
        args.push_back(config.pch_file->string());
    }
    args.push_back("-fsyntax-only");
    for (const auto& source : sources) {
        args.push_back(source.string());
    }

    auto run = GUARD(build::process_bridge::run(
        config.compiler, args,
        build::process_bridge::RunOptions{
            .cancel_requested = cancel_requested,
            .capture_policy = build::process_bridge::CapturePolicy::BufferedDiagnostics,
        }));
    if (run.canceled) {
        return util::process::ProcessMetrics{};
    }
    if (run.exit_code != 0) {
        return std::unexpected(util::make_error("Check batch failed."));
    }
    return run.metrics.value_or(util::process::ProcessMetrics{});
}

}  // namespace build::compile::detail


