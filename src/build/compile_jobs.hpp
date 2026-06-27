#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "build/compile.hpp"

namespace build::compile::detail {

auto env_var_present(const char* name) -> bool;
auto parse_forced_jobs_override() -> std::optional<int>;
auto query_available_memory_mb() -> std::optional<std::uint64_t>;

auto mode_name(CompileMode mode) -> std::string_view;
auto scheduler_trace_enabled() -> bool;
auto incremental_trace_enabled() -> bool;

auto compile_flags(const core::Manifest& manifest, bool release,
                   bool exceptions_enabled) -> std::vector<std::string>;
auto include_paths_for_manifest(const std::filesystem::path& root,
                                const core::Manifest& manifest,
                                std::span<const std::filesystem::path>
                                    dependency_include_paths)
    -> std::vector<std::filesystem::path>;
auto compiler_for_manifest(const core::Manifest& manifest)
    -> std::filesystem::path;

}  // namespace build::compile::detail
