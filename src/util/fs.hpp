#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "util/result.hpp"

namespace util::fs {

auto temporary_path_for(const std::filesystem::path& path,
                        std::string_view tag = "tmp")
    -> std::filesystem::path;
auto atomic_replace_file(const std::filesystem::path& staged_path,
                         const std::filesystem::path& final_path)
    -> util::Status;
auto publish_staged_file(const std::filesystem::path& staged_path,
                         const std::filesystem::path& final_path)
    -> util::Status;
auto publish_staged_directory(const std::filesystem::path& staged_path,
                              const std::filesystem::path& final_path)
    -> util::Status;
auto atomic_write_text(const std::filesystem::path& path,
                       std::string_view content) -> util::Status;
auto atomic_write_text_result(const std::filesystem::path& path,
                              std::string_view content) -> util::Status;

inline auto path_key(const std::filesystem::path& path) -> std::string {
    return path.generic_string();
}

inline auto normalized_path_key(const std::filesystem::path& path)
    -> std::string {
    return path.lexically_normal().generic_string();
}

}  // namespace util::fs
