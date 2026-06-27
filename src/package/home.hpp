#pragma once

#include <filesystem>

#include "util/result.hpp"

namespace package::home {

auto root() -> util::Result<std::filesystem::path>;
auto ensure_layout() -> util::Result<std::filesystem::path>;
auto registry_index_dir() -> util::Result<std::filesystem::path>;
auto registry_cache_dir() -> util::Result<std::filesystem::path>;
auto registry_src_dir() -> util::Result<std::filesystem::path>;
auto git_checkout_dir() -> util::Result<std::filesystem::path>;
auto credentials_file() -> util::Result<std::filesystem::path>;

}  // namespace package::home
