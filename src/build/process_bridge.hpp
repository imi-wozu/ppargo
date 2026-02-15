#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "util/result.hpp"


namespace build::process_bridge {

auto run(const std::filesystem::path& program,
         const std::vector<std::string>& args) -> util::Result<int>;

}  // namespace build::process_bridge



