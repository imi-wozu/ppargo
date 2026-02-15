#pragma once

#include <filesystem>
#include <regex>
#include <string>
#include <vector>

#include "util/result.hpp"


namespace build::graph {

using CompiledExcludes = std::vector<std::regex>;

auto glob_to_regex(const std::string& pattern) -> std::string;
auto compile_excludes(const std::vector<std::string>& patterns) -> CompiledExcludes;
auto matches_excludes(const std::string& relative_path,
                      const CompiledExcludes& compiled_patterns) -> bool;
auto matches_excludes(const std::string& relative_path,
                      const std::vector<std::string>& patterns) -> bool;
auto object_path_for_source(
    const std::filesystem::path& source_root, const std::filesystem::path& obj_root,
    const std::filesystem::path& source_file)
    -> util::Result<std::filesystem::path>;

}  // namespace build::graph



