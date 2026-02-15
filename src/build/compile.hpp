#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "core/manifest.hpp"
#include "util/result.hpp"


namespace build::compile {

struct CompilerConfig {
    std::filesystem::path compiler;
    std::vector<std::string> flags;
    std::vector<std::filesystem::path> include_paths;
    int jobs = 1;
};

struct CompileResult {
    std::vector<std::filesystem::path> objects;
    std::size_t compiled_count = 0;
};

auto make_compiler_config(const std::filesystem::path& root,
                          const core::Manifest& manifest,
                          bool release) -> util::Result<CompilerConfig>;

auto compile_signature(const CompilerConfig& config) -> std::string;
auto refresh_object_dir_if_signature_changed(
    const std::filesystem::path& build_root,
    const std::string& signature) -> util::Status;

auto compile_objects(
    const std::filesystem::path& source_root, const std::filesystem::path& obj_root,
    const std::vector<std::filesystem::path>& sources, const CompilerConfig& config,
    bool force_rebuild) -> util::Result<CompileResult>;

auto run_checks_with_cache(const std::filesystem::path& root,
                           const std::filesystem::path& build_root,
                           const std::vector<std::filesystem::path>& sources,
                           const CompilerConfig& config) -> util::Status;

}  // namespace build::compile



