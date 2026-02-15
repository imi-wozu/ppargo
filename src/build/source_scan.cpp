#include "build/source_scan.hpp"

#include <algorithm>
#include <format>
#include <string>

#include "build/graph.hpp"


namespace {

auto normalize_path(const std::filesystem::path& path) {
    return path.generic_string();
}

}  // namespace

namespace build::source_scan {

auto collect_sources(const std::filesystem::path& root, const core::Manifest& manifest)
    -> util::Result<std::vector<std::filesystem::path>> {
    const std::filesystem::path source_root = root / manifest.build.source_dir;
    std::error_code ec;
    if (!std::filesystem::exists(source_root, ec) || ec) {
        return std::unexpected(std::format(
            "Build Error: Source directory not found: {}", source_root.string()));
    }

    const std::filesystem::path canonical_source_root = std::filesystem::weakly_canonical(source_root, ec);
    if (ec) {
        return std::unexpected(std::format(
            "Build Error: Failed to canonicalize source directory: {}",
            source_root.string()));
    }

    const auto compiled_excludes = build::graph::compile_excludes(manifest.build.exclude);

    std::vector<std::filesystem::path> sources;
    for (std::filesystem::recursive_directory_iterator it(source_root, ec), end; it != end;
         it.increment(ec)) {
        if (ec) {
            return std::unexpected(std::format(
                "Build Error: Failed while scanning source directory: {}",
                source_root.string()));
        }

        if (!it->is_regular_file(ec)) {
            if (ec) {
                return std::unexpected(std::format(
                    "Build Error: Failed to inspect source entry: {}",
                    it->path().string()));
            }
            continue;
        }

        const auto ext = it->path().extension().string();
        if (ext != ".cpp" && ext != ".cc" && ext != ".cxx" && ext != ".c") {
            continue;
        }

        const auto relative_to_root_path = std::filesystem::relative(it->path(), root, ec);
        if (ec) {
            return std::unexpected(std::format(
                "Build Error: Failed to resolve source path: {}", it->path().string()));
        }
        const auto relative_to_root = normalize_path(relative_to_root_path);
        if (build::graph::matches_excludes(relative_to_root, compiled_excludes)) {
            continue;
        }

        const auto canonical_source = std::filesystem::weakly_canonical(it->path(), ec);
        if (ec) {
            return std::unexpected(std::format(
                "Build Error: Failed to canonicalize source file: {}",
                it->path().string()));
        }

        const auto relative_to_source =
            std::filesystem::relative(canonical_source, canonical_source_root, ec);
        if (ec || relative_to_source.empty() ||
            *relative_to_source.begin() == std::filesystem::path("..")) {
            return std::unexpected(std::format(
                "Build Error: Source file resolved outside configured source_dir: {}",
                canonical_source.string()));
        }

        sources.push_back(canonical_source);
    }

    std::sort(sources.begin(), sources.end());
    return sources;
}

}  // namespace build::source_scan


