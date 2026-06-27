#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace core {

auto current_executable_path() -> std::optional<std::filesystem::path>;
auto host_triple() -> std::string;
auto os_description() -> std::string;

}  // namespace core
