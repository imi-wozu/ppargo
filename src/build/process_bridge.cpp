#include "build/process_bridge.hpp"

#include <span>
#include <string>
#include <string_view>

#include "util/environment.hpp"
#include "util/output.hpp"
#include "util/process.hpp"
namespace {

auto quote_string(std::string_view  value) -> std::string {
    return std::string{"\""} + std::string(value) + "\"";
}

void flush_captured_output(std::string_view output, bool fatal) {
    if (output.empty()) {
        return;
    }

    std::size_t start = 0;
    while (start < output.size()) {
        const auto end = output.find('\n', start);
        const auto line_end = end == std::string_view::npos ? output.size() : end;
        auto line = output.substr(start, line_end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (!line.empty()) {
            util::output::child_diagnostic_line(line, fatal);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
}

auto make_process_options(const build::process_bridge::RunOptions& options,
                          bool capture_output) -> util::process::RunOptions {
    return util::process::RunOptions{
        .working_directory = std::nullopt,
        .capture_output = capture_output,
        .merge_stderr = capture_output,
        .stdin_mode = util::process::StdinMode::Null,
        .collect_metrics = true,
        .cancel_requested = options.cancel_requested,
        .timeout_ms = std::nullopt,
    };
}

auto run_once(const std::filesystem::path& program,
              std::span<const std::string>  args,
              const build::process_bridge::RunOptions& options,
              bool capture_output) -> util::Result<util::process::RunResult> {
    return util::process::run_result(program, args,
                                     make_process_options(options, capture_output));
}

}  // namespace

namespace build::process_bridge {

auto run(const std::filesystem::path& program,
         std::span<const std::string>  args,
         const RunOptions& options) -> util::Result<util::process::RunResult> {
    const bool trace = util::env::exists("PPARGO_TRACE");
    if (trace) {
        std::string command_line = quote_string(program.string());
        for (const auto& arg : args) {
            command_line += " " + quote_string(arg);
        }
        util::output::trace(command_line);
    }

    const bool fast_no_capture =
        options.capture_policy == CapturePolicy::FastNoCapture && !trace;
    const bool buffered_diagnostics =
        options.capture_policy == CapturePolicy::BufferedDiagnostics && !trace;

    auto first =
        run_once(program, args, options, !fast_no_capture || buffered_diagnostics);
    if (!first) {
        return std::unexpected(first.error());
    }

    if (first->canceled) {
        return first;
    }

    if (fast_no_capture && first->exit_code != 0) {
        auto retry = run_once(program, args, options, true);
        if (!retry) {
            return std::unexpected(retry.error());
        }

        if ((!retry->canceled && retry->exit_code != 0) || trace) {
            flush_captured_output(retry->output, true);
        }
        return retry;
    }

    if (buffered_diagnostics && !first->canceled && first->exit_code == 0 &&
        !first->output.empty()) {
        flush_captured_output(first->output, false);
    }

    if (((!first->canceled && first->exit_code != 0) || trace) &&
        !first->output.empty()) {
        flush_captured_output(first->output, true);
    }

    return first;
}

auto run(const std::filesystem::path& program,
         std::span<const std::string>  args,
         const std::atomic_bool* cancel_requested)
    -> util::Result<util::process::RunResult> {
    return run(program, args,
               RunOptions{.cancel_requested = cancel_requested,
                          .capture_policy = CapturePolicy::Always});
}

}  // namespace build::process_bridge
