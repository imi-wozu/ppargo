#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "core/manifest.hpp"
#include "package/backend.hpp"
#include "util/result.hpp"

namespace package::backends::ppargo_backend {
auto resolve(const std::filesystem::path& project_root,
             const core::Manifest& manifest,
             const FeatureOptions& features = {}) -> util::Result<ResolvedGraph>;
auto fetch(const std::filesystem::path& project_root,
           const core::Manifest& manifest,
           const ResolvedGraph& graph) -> util::Status;
auto install(const std::filesystem::path& project_root,
             const core::Manifest& manifest,
             const ResolvedGraph& graph) -> util::Status;

auto publish(const std::filesystem::path& project_root,
             const core::Manifest& manifest) -> util::Status;
auto yank(const std::filesystem::path& project_root,
          const core::Manifest& manifest,
          const YankRequest& request) -> util::Status;
auto owner_add(const std::filesystem::path& project_root,
               const core::Manifest& manifest,
               std::string_view  owner) -> util::Status;
auto owner_remove(const std::filesystem::path& project_root,
                  const core::Manifest& manifest,
                  std::string_view  owner) -> util::Status;
auto auth_login(const std::filesystem::path& project_root,
                const core::Manifest& manifest,
                std::string_view  registry,
                std::string_view  token) -> util::Status;
auto auth_logout(const std::filesystem::path& project_root,
                 const core::Manifest& manifest,
                 std::string_view  registry) -> util::Status;

}  // namespace package::backends::ppargo_backend
