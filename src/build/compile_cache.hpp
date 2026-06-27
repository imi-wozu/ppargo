#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

#include "build/compile.hpp"

namespace build::compile::detail {

struct CheckCacheEntry {
    long long mtime = 0;
    std::uintmax_t size = 0;
    long long dependency_recency = 0;
};

auto recreate_directory(const std::filesystem::path& dir) -> util::Status;
auto file_stamp(const std::filesystem::path& path) -> util::Result<long long>;
auto file_size_value(const std::filesystem::path& path)
    -> util::Result<std::uintmax_t>;
auto signature_hash_text(std::string_view  signature) -> std::string;

auto load_check_cache(const std::filesystem::path& cache_file,
                      std::string_view  signature_hash)
    -> std::unordered_map<std::string, CheckCacheEntry>;
auto write_check_cache(
    const std::filesystem::path& cache_file, std::string_view  signature_hash,
    const std::unordered_map<std::string, CheckCacheEntry>& cache) -> util::Status;

}  // namespace build::compile::detail
