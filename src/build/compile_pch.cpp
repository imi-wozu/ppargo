#include "build/compile_pch.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "build/compile_cache.hpp"
#include "build/compile_jobs.hpp"
#include "build/process_bridge.hpp"
#include "util/fs.hpp"
#include "util/output.hpp"
#include "util/text.hpp"

namespace {

struct PchLayout {
    std::filesystem::path root;
    std::filesystem::path header;
    std::filesystem::path pch;
    std::filesystem::path signature;
};

auto include_directive_from_line(std::string_view line) -> std::optional<std::string> {
    const auto trimmed = util::text::trim_ascii_view(line);
    if (!trimmed.starts_with("#include")) {
        return std::nullopt;
    }

    const auto include_target = util::text::trim_ascii_view(trimmed.substr(8));
    if (include_target.empty()) {
        return std::nullopt;
    }

    if (include_target.front() != '<' && include_target.front() != '"') {
        return std::nullopt;
    }

    return std::format("#include {}", include_target);
}

auto pch_layout_for_build_root(const std::filesystem::path& build_root) -> PchLayout {
    const auto root = build_root / "pch";
    return PchLayout{
        .root = root,
        .header = root / "ppargo_auto_pch.hpp",
        .pch = root / "ppargo_auto_pch.pch",
        .signature = root / ".pch_signature",
    };
}

auto collect_common_headers_for_pch(
    std::span<const std::filesystem::path> sources,
    const build::settings::ResolvedBuildSettings& settings)
    -> std::vector<std::string> {
    if (sources.empty()) {
        return {};
    }

    std::unordered_map<std::string, std::size_t> counts;
    for (const auto& source : sources) {
        std::ifstream input(source);
        if (!input.is_open()) {
            continue;
        }

        std::unordered_set<std::string> unique_for_file;
        std::string line;
        for (std::size_t line_index = 0; std::getline(input, line); ++line_index) {
            if (settings.pch_scan_lines != 0 &&
                line_index >= settings.pch_scan_lines) {
                break;
            }
            if (const auto include = include_directive_from_line(line);
                include.has_value()) {
                unique_for_file.insert(*include);
            }
        }

        for (const auto& include : unique_for_file) {
            ++counts[include];
        }
    }

    const std::size_t threshold = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(
               static_cast<double>(sources.size()) *
               settings.pch_frequency_threshold)));

    std::vector<std::string> headers;
    headers.reserve(counts.size());
    for (const auto& [include, count] : counts) {
        if (count >= threshold) {
            headers.push_back(include);
        }
    }

    std::sort(headers.begin(), headers.end());
    if (headers.size() > settings.pch_max_headers) {
        headers.resize(settings.pch_max_headers);
    }
    return headers;
}

auto pch_signature_for(const build::compile::CompilerConfig& config,
                       std::span<const std::string> headers) -> std::string {
    std::ostringstream out;
    out << config.compiler.generic_string() << "\n";
    out << build::compile::detail::mode_name(config.mode) << "\n";
    for (const auto& flag : config.flags) {
        out << flag << "\n";
    }
    out << "--includes--\n";
    for (const auto& include : config.include_paths) {
        out << include.generic_string() << "\n";
    }
    out << "--headers--\n";
    for (const auto& header : headers) {
        out << header << "\n";
    }
    return out.str();
}

}  // namespace

namespace build::compile::detail {

auto ensure_auto_pch(const CompilerConfig& config,
                     const std::filesystem::path& build_root,
                     std::span<const std::filesystem::path> selected_sources)
    -> std::optional<std::filesystem::path> {
    const auto headers =
        collect_common_headers_for_pch(selected_sources, config.build_settings);
    if (headers.empty()) {
        return std::nullopt;
    }

    const auto layout = pch_layout_for_build_root(build_root);
    std::error_code ec;
    std::filesystem::create_directories(layout.root, ec);
    if (ec) {
        if (build::compile::detail::scheduler_trace_enabled()) {
            util::output::trace(std::format(
                "optimizer: pch disabled (directory create failed: {} ({}))",
                layout.root.string(), ec.message()));
        }
        return std::nullopt;
    }

    const auto signature = pch_signature_for(config, headers);
    const auto signature_hash =
        build::compile::detail::signature_hash_text(signature);

    if (std::filesystem::exists(layout.signature, ec) && !ec &&
        std::filesystem::exists(layout.pch, ec) && !ec) {
        std::ifstream signature_file(layout.signature);
        std::string existing;
        if (signature_file.is_open() && std::getline(signature_file, existing) &&
            existing == signature_hash) {
            return layout.pch;
        }
    }

    std::ostringstream header_content;
    for (const auto& header : headers) {
        header_content << header << "\n";
    }

    if (!util::fs::atomic_write_text_result(layout.header, header_content.str())) {
        return std::nullopt;
    }

    std::vector<std::string> args;
    args.reserve(config.flags.size() + config.include_paths.size() * 2 + 8);
    for (const auto& flag : config.flags) {
        args.push_back(flag);
    }
    for (const auto& include_path : config.include_paths) {
        args.push_back("-I");
        args.push_back(include_path.string());
    }
    args.push_back("-x");
    args.push_back("c++-header");
    args.push_back(layout.header.string());
    args.push_back("-o");
    args.push_back(layout.pch.string());

    auto run = build::process_bridge::run(
        config.compiler, args,
        build::process_bridge::RunOptions{
            .cancel_requested = nullptr,
            .capture_policy = build::process_bridge::CapturePolicy::BufferedDiagnostics,
        });
    if (!run || run->canceled || run->exit_code != 0) {
        std::filesystem::remove(layout.pch, ec);
        std::filesystem::remove(layout.signature, ec);
        if (build::compile::detail::scheduler_trace_enabled()) {
            util::output::trace(
                "optimizer: pch build failed; continuing without pch");
        }
        return std::nullopt;
    }

    if (!util::fs::atomic_write_text_result(layout.signature, signature_hash)) {
        return std::nullopt;
    }

    if (build::compile::detail::scheduler_trace_enabled()) {
        util::output::trace(std::format("optimizer: pch enabled ({})",
                                        layout.pch.string()));
    }

    return layout.pch;
}

}  // namespace build::compile::detail
