#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "core/lockfile.hpp"
#include "core/manifest.hpp"
#include "package/backend.hpp"
#include "util/result.hpp"

namespace package::detail {

struct ResolveOptions {
    bool force_refresh = false;
    bool locked = false;
    bool offline = false;
    package::FeatureOptions features;
};

auto manifest_fingerprint(const core::Manifest& manifest,
                          const package::FeatureOptions& features = {})
    -> util::Result<std::string>;

auto graph_from_lock(const core::Lockfile& lockfile) -> package::ResolvedGraph;
auto lock_from_graph(std::string_view  fingerprint, const core::Manifest& manifest,
                     const package::ResolvedGraph& graph,
                     const package::FeatureOptions& features = {})
    -> core::Lockfile;

auto resolve_with_lock(const std::filesystem::path& project_root,
                       const core::Manifest& manifest,
                       const ResolveOptions& options = {})
    -> util::Result<package::ResolvedGraph>;

}  // namespace package::detail
