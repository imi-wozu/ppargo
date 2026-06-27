#pragma once

#include <cstddef>
#include <string>

#include "core/manifest.hpp"

namespace build::settings {

struct ResolvedBuildSettings {
    std::size_t aggressive_tu_threshold = 24;
    std::size_t aggressive_stale_threshold = 8;
    std::size_t pch_scan_lines = 200;
    double pch_frequency_threshold = 0.60;
    std::size_t pch_max_headers = 40;
    int depscan_timeout_ms = 10000;
};

struct LinkConcurrency {
    int jobs = 1;
    std::string reason;
};

auto resolve(const core::Manifest& manifest) -> ResolvedBuildSettings;
auto resolve_link_concurrency(std::size_t runnable_link_actions)
    -> LinkConcurrency;

}  // namespace build::settings
