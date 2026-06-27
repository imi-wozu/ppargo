#pragma once

#include <filesystem>
#include <regex>
#include <string>
#include <string_view>
#include <vector>
#include <span>

#include "util/result.hpp"


namespace build::graph {

using CompiledExcludes = std::vector<std::regex>;

auto glob_to_regex(std::string_view  pattern) -> std::string;
auto compile_excludes(std::span<const std::string>  patterns) -> CompiledExcludes;
auto matches_excludes(std::string_view  relative_path,
                      const CompiledExcludes& compiled_patterns) -> bool;
auto matches_excludes(std::string_view  relative_path,
                      std::span<const std::string>  patterns) -> bool;
auto object_path_for_source(
    const std::filesystem::path& source_root, const std::filesystem::path& obj_root,
    const std::filesystem::path& source_file)
    -> util::Result<std::filesystem::path>;

}  // namespace build::graph



