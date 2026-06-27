#pragma once

#include <filesystem>
#include <optional>
#include <span>

#include "build/compile.hpp"

namespace build::compile::detail {

auto ensure_auto_pch(const CompilerConfig& config,
                     const std::filesystem::path& build_root,
                     std::span<const std::filesystem::path> selected_sources)
    -> std::optional<std::filesystem::path>;

}  // namespace build::compile::detail
