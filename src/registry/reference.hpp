#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "core/manifest.hpp"
#include "package/backend.hpp"
#include "util/result.hpp"

namespace registry::reference {

auto resolve_registry_package(const core::Manifest& manifest,
                              std::string_view  registry,
                              std::string_view  name,
                              std::string_view  requirement)
    -> util::Result<package::ResolvedPackage>;

auto fetch_registry_package(const core::Manifest& manifest,
                            const package::ResolvedPackage& package) -> util::Status;
auto publish_package(const std::filesystem::path& project_root,
                     const core::Manifest& manifest,
                     std::string_view  registry) -> util::Status;
auto yank_package(const core::Manifest& manifest,
                  std::string_view  registry,
                  std::string_view  package_name,
                  std::string_view  version,
                  bool undo) -> util::Status;
auto add_owner(const core::Manifest& manifest,
               std::string_view  registry,
               std::string_view  package_name,
               std::string_view  owner) -> util::Status;
auto remove_owner(const core::Manifest& manifest,
                  std::string_view  registry,
                  std::string_view  package_name,
                  std::string_view  owner) -> util::Status;
auto login(std::string_view  registry, std::string_view  token) -> util::Status;
auto logout(std::string_view  registry) -> util::Status;

}  // namespace registry::reference
