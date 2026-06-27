#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <span>

#include "build/compile.hpp"
#include "util/result.hpp"

namespace build::detail {

struct LinkFailureInfo {
    int exit_code = -1;
    std::string output;
};

struct LinkPipelineResult {
    bool success = false;
    std::optional<util::Error> error;
    std::optional<LinkFailureInfo> failure;
};

auto make_link_signature(const compile::CompilerConfig& config,
                         const std::filesystem::path& output,
                         std::span<const std::filesystem::path>  library_paths,
                         std::span<const std::string>  libraries, bool release)
    -> std::string;

auto needs_link(std::span<const std::filesystem::path>  objects,
                const std::filesystem::path& output, std::size_t compiled_count,
                std::span<const std::filesystem::path>  library_paths,
                std::span<const std::string>  libraries,
                std::string_view  link_signature,
                const std::filesystem::path& link_signature_file) -> bool;

auto execute_link_pipeline(
    const compile::CompilerConfig& config, const std::filesystem::path& build_root,
    std::span<const std::filesystem::path>  objects,
    const std::filesystem::path& output,
    std::span<const std::filesystem::path>  library_paths,
    std::span<const std::string>  libraries,
    std::span<const std::filesystem::path> runtime_files,
    const std::filesystem::path& link_signature_file,
    std::string_view  link_signature, bool release) -> LinkPipelineResult;

}  // namespace build::detail
