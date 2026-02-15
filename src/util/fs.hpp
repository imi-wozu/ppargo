#pragma once

#include <filesystem>
#include <string_view>

#include "util/result.hpp"


namespace util::fs {

auto atomic_write_text(const std::filesystem::path& path,
                       std::string_view content) -> util::Status;
auto atomic_write_text_result(const std::filesystem::path& path,
                              std::string_view content) -> util::Status;

}  // namespace util::fs




