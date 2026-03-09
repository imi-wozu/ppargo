#pragma once

#include <filesystem>
#include <string_view>

#include "util/result.hpp"

namespace core {

auto validate_project_name(std::string_view name) -> util::Status;
auto create_project_structure(const std::filesystem::path& root,
                              std::string_view name) -> util::Status;

}  // namespace core
