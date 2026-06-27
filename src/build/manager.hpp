#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

#include "build/targets.hpp"
#include "core/dependency_artifacts.hpp"
#include "core/manifest.hpp"
#include "util/result.hpp"

namespace build {

struct BuildOptions {
    enum class Mode { Build, Check };

    bool release{false};
    Mode mode{Mode::Build};
    std::optional<targets::Selection> target;
    std::optional<std::filesystem::path> output_dir_override;
    core::DependencyArtifacts dependency_artifacts;
};

struct BuildExecutionResult {
    std::size_t compiled_units{0};
    bool linked{false};
    std::filesystem::path output_binary;
    std::string target_name;
};

auto execute(const std::filesystem::path& root, const core::Manifest& manifest,
             const BuildOptions& options) -> util::Result<BuildExecutionResult>;

}  // namespace build
