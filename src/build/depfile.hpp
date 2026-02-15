#pragma once

#include <filesystem>
#include <vector>

#include "util/result.hpp"


namespace build::depfile {

auto parse_dependencies(const std::filesystem::path& dep_file)
    -> util::Result<std::vector<std::filesystem::path>>;

}  // namespace build::depfile



