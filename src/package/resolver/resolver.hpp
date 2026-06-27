#pragma once

#include <filesystem>

#include "core/manifest.hpp"
#include "package/backend.hpp"
#include "util/result.hpp"

namespace package::resolver {

auto resolve(const std::filesystem::path& project_root,
             const core::Manifest& manifest,
             const FeatureOptions& features = {}) -> util::Result<ResolvedGraph>;

}  // namespace package::resolver
