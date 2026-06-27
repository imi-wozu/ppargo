#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "build/targets.hpp"
#include "core/manifest.hpp"
#include "util/result.hpp"

namespace build::detail {

struct CollectedSources {
    std::filesystem::path source_root;
    std::vector<std::filesystem::path> sources;
};

auto collect_target_sources(const std::filesystem::path& root,
                            const core::Manifest& manifest,
                            const build::targets::ResolvedBuildTarget& resolved_target,
                            build::targets::SelectionKind selected_kind,
                            const std::optional<std::filesystem::path>& output_dir_override)
    -> util::Result<CollectedSources>;

}  // namespace build::detail
