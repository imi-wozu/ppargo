#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

#include "core/manifest.hpp"
#include "util/result.hpp"

namespace core {

auto find_project_root(const std::filesystem::path& start = {}) noexcept
    -> util::Result<std::filesystem::path>;

auto build_dir(const std::filesystem::path& root, const Manifest& manifest,
               bool release) -> std::filesystem::path;
auto build_dir(const std::filesystem::path& root, const Manifest& manifest,
               bool release,
               const std::optional<std::filesystem::path>& output_dir_override)
    -> std::filesystem::path;
auto effective_output_dir(
    const Manifest& manifest,
    const std::optional<std::filesystem::path>& output_dir_override)
    -> std::filesystem::path;
auto effective_module_output_dir(
    const Manifest& manifest,
    const std::optional<std::filesystem::path>& output_dir_override)
    -> std::filesystem::path;

auto runner_cache_dir(const std::filesystem::path& root,
                      const Manifest& manifest) -> std::filesystem::path;

auto binary_name(const Manifest& manifest) -> std::string;

constexpr auto detect_triplet() noexcept -> std::string_view {
#if defined(_WIN32)
#define OS_STR "windows"
#elif defined(__APPLE__)
#define OS_STR "osx"
#else
#define OS_STR "linux"
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
#define ARCH_STR "arm64"
#else
#define ARCH_STR "x64"
#endif
    return ARCH_STR "-" OS_STR;
}

}  // namespace core
