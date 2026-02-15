#pragma once

#include <filesystem>
#include <string>

#include "core/manifest.hpp"
#include "util/result.hpp"


namespace core {

auto find_project_root(
    const std::filesystem::path& start = {})
    -> util::Result<std::filesystem::path>;

auto build_dir(const std::filesystem::path& root, const Manifest& manifest,
               bool release) -> std::filesystem::path;

auto binary_name(const Manifest& manifest) -> std::string;

auto detect_triplet() -> std::string;

}  // namespace core



