#pragma once

#include <filesystem>
#include <string>

#include "util/result.hpp"


namespace core {

auto validate_project_name(const std::string& name) -> util::Status;
auto create_project_structure(const std::filesystem::path& root,
                              const std::string& name) -> util::Status;

}  // namespace core



