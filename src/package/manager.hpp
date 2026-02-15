#pragma once

#include <filesystem>

#include "core/manifest.hpp"
#include "util/result.hpp"


namespace package {

auto install_dependencies(const std::filesystem::path& project_root,
                          const core::Manifest& manifest) -> util::Status;
auto add_dependency(const std::filesystem::path& project_root,
                    const std::string& package_name) -> util::Status;

}  // namespace package



