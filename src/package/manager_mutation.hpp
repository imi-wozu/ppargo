#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "core/manifest.hpp"
#include "util/result.hpp"

namespace package::detail {

struct ParsedDependencyInput {
    std::string name;
    std::optional<std::string> version;
};

auto validate_package_name(std::string_view  package_name) -> util::Status;
auto parse_dependency_input(std::string_view  dep_spec)
    -> util::Result<ParsedDependencyInput>;

auto infer_version_for_add(const std::filesystem::path& project_root,
                           const core::Manifest& manifest,
                           std::string_view  dependency_name)
    -> util::Result<std::string>;

auto load_project_manifest(const std::filesystem::path& project_root)
    -> util::Result<core::Manifest>;
auto save_project_manifest(const std::filesystem::path& project_root,
                           const core::Manifest& manifest) -> util::Status;
auto save_project_dependencies(const std::filesystem::path& project_root,
                               const core::Manifest& manifest) -> util::Status;

}  // namespace package::detail
