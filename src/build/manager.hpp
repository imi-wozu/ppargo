#pragma once

#include <cstddef>
#include <filesystem>

#include "core/manifest.hpp"
#include "util/result.hpp"


namespace build {

struct BuildOptions {
    enum class Mode { Build, Check };

    bool release = false;
    Mode mode = Mode::Build;
};

struct BuildExecutionResult {
    std::size_t compiled_units = 0;
    bool linked = false;
};

auto execute(const std::filesystem::path& root, const core::Manifest& manifest,
             const BuildOptions& options) -> util::Result<BuildExecutionResult>;
auto build(const std::filesystem::path& root, const core::Manifest& manifest,
           bool release) -> util::Status;
auto check(const std::filesystem::path& root, const core::Manifest& manifest,
           bool release) -> util::Status;

}  // namespace build



