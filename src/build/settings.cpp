#include "build/settings.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <string_view>
#include <thread>

#include "util/environment.hpp"

namespace build::settings {

namespace {

auto parse_non_negative_env(std::string_view name) -> std::optional<int> {
    const auto value = util::env::get(name);
    if (!value.has_value()) {
        return std::nullopt;
    }

    int parsed = 0;
    const auto* begin = value->data();
    const auto* end = begin + value->size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end || parsed < 0) {
        return std::nullopt;
    }
    return parsed;
}

auto parse_positive_env(std::string_view name) -> std::optional<int> {
    const auto parsed = parse_non_negative_env(name);
    if (!parsed.has_value() || *parsed <= 0) {
        return std::nullopt;
    }
    return parsed;
}

auto parse_probability_env(std::string_view name) -> std::optional<double> {
    const auto value = util::env::get(name);
    if (!value.has_value()) {
        return std::nullopt;
    }

    double parsed = 0.0;
    const auto* begin = value->data();
    const auto* end = begin + value->size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end || parsed < 0.0 || parsed > 1.0) {
        return std::nullopt;
    }
    return parsed;
}

}  // namespace

auto resolve(const core::Manifest& manifest) -> ResolvedBuildSettings {
    ResolvedBuildSettings resolved;
    resolved.aggressive_tu_threshold = static_cast<std::size_t>(
        std::max(0, manifest.build.aggressive_tu_threshold));
    resolved.aggressive_stale_threshold = static_cast<std::size_t>(
        std::max(0, manifest.build.aggressive_stale_threshold));
    resolved.pch_scan_lines =
        static_cast<std::size_t>(std::max(0, manifest.build.pch_scan_lines));
    resolved.pch_frequency_threshold =
        std::clamp(manifest.build.pch_frequency_threshold, 0.0, 1.0);
    resolved.pch_max_headers =
        static_cast<std::size_t>(std::max(0, manifest.build.pch_max_headers));
    resolved.depscan_timeout_ms = std::max(1, manifest.build.depscan_timeout_ms);

    if (const auto value = parse_non_negative_env("PPARGO_AGGRESSIVE_TU_THRESHOLD");
        value.has_value()) {
        resolved.aggressive_tu_threshold = static_cast<std::size_t>(*value);
    }
    if (const auto value =
            parse_non_negative_env("PPARGO_AGGRESSIVE_STALE_THRESHOLD");
        value.has_value()) {
        resolved.aggressive_stale_threshold = static_cast<std::size_t>(*value);
    }
    if (const auto value = parse_non_negative_env("PPARGO_PCH_SCAN_LINES");
        value.has_value()) {
        resolved.pch_scan_lines = static_cast<std::size_t>(*value);
    }
    if (const auto value = parse_probability_env("PPARGO_PCH_FREQUENCY_THRESHOLD");
        value.has_value()) {
        resolved.pch_frequency_threshold = *value;
    }
    if (const auto value = parse_non_negative_env("PPARGO_PCH_MAX_HEADERS");
        value.has_value()) {
        resolved.pch_max_headers = static_cast<std::size_t>(*value);
    }
    if (const auto value = parse_positive_env("PPARGO_DEPSCAN_TIMEOUT_MS");
        value.has_value()) {
        resolved.depscan_timeout_ms = *value;
    }

    return resolved;
}

auto resolve_link_concurrency(std::size_t runnable_link_actions)
    -> LinkConcurrency {
    LinkConcurrency resolved;
    if (runnable_link_actions == 0) {
        resolved.jobs = 1;
        resolved.reason = "no link actions";
        return resolved;
    }

    if (const auto override_value = parse_positive_env("PPARGO_FORCE_LINK_JOBS");
        override_value.has_value()) {
        resolved.jobs = std::max(
            1, std::min(static_cast<int>(runnable_link_actions),
                        std::clamp(*override_value, 1, 64)));
        resolved.reason = "forced override=" + std::to_string(resolved.jobs);
        return resolved;
    }

    const unsigned hw_threads = std::thread::hardware_concurrency();
    const auto hw_cap = std::clamp<int>(
        hw_threads == 0 ? 1 : static_cast<int>(hw_threads), 1, 64);
    const auto default_jobs = std::min(2, std::max(1, hw_cap / 4));
    resolved.jobs = std::max(
        1, std::min(static_cast<int>(runnable_link_actions), default_jobs));
    resolved.reason = "auto=" + std::to_string(resolved.jobs);
    return resolved;
}

}  // namespace build::settings
