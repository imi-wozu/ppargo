#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "build/compile.hpp"
#include "util/result.hpp"


namespace build::link {

auto ensure_not_running_target(const std::filesystem::path& output) -> util::Status;

auto link_binary(const compile::CompilerConfig& config,
                 const std::vector<std::filesystem::path>& object_files,
                 const std::filesystem::path& output,
                 const std::vector<std::filesystem::path>& library_paths,
                 const std::vector<std::string>& libraries,
                 bool release) -> util::Status;

auto copy_runtime_dlls(const std::filesystem::path& root,
                       const std::filesystem::path& build_dir) -> util::Status;

}  // namespace build::link



