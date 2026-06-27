#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <span>

#include "build/targets.hpp"
#include "util/result.hpp"

namespace build::targets::detail {

auto is_cpp_source_extension(std::string_view ext) -> bool;
auto is_cpp_source_path(const std::filesystem::path& path) -> bool;
auto is_regular_file_path(const std::filesystem::path& path) -> bool;
auto is_directory_path(const std::filesystem::path& path) -> bool;

auto canonical_existing_path(const std::filesystem::path& path)
    -> util::Result<std::filesystem::path>;

auto collect_recursive_cpp_sources(const std::filesystem::path& root)
    -> util::Result<std::vector<std::filesystem::path>>;

auto sort_targets(std::vector<Target>& targets) -> void;
auto format_named_choices(std::span<const Target>  targets) -> std::string;

}  // namespace build::targets::detail
