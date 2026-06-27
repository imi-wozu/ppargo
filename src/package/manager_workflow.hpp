#pragma once

#include <filesystem>
#include <utility>

#include "core/manifest.hpp"
#include "package/manager_mutation.hpp"
#include "util/result.hpp"

namespace package::detail {

auto require_package_management_enabled(const core::Manifest& manifest)
    -> util::Status;
auto resolve_fetch_install(const std::filesystem::path& project_root,
                           const core::Manifest& manifest, bool force_refresh)
    -> util::Status;
auto resolve_and_fetch_only(const std::filesystem::path& project_root,
                            const core::Manifest& manifest, bool force_refresh)
    -> util::Status;

template <typename Fn>
auto with_project_manifest(const std::filesystem::path& project_root, Fn&& action)
    -> util::Status {
    auto manifest = GUARD(load_project_manifest(project_root));
    return std::forward<Fn>(action)(manifest);
}

}  // namespace package::detail
