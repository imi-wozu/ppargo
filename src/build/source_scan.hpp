#pragma once

#include <filesystem>
#include <vector>

#include "core/manifest.hpp"
#include "util/result.hpp"


namespace build::source_scan {

auto collect_sources(const std::filesystem::path& root,
                     const core::Manifest& manifest)
    -> util::Result<std::vector<std::filesystem::path>>;

}  // namespace build::source_scan



