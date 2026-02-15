#include "build/fingerprint.hpp"

#include <optional>
#include <vector>

#include "build/depfile.hpp"


namespace {

auto exists_file(const std::filesystem::path& path) -> bool {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

auto read_time(const std::filesystem::path& path) -> std::optional<std::filesystem::file_time_type> {
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::nullopt;
    }
    return time;
}

auto resolve_dependency_path(const std::filesystem::path& source_root, const std::filesystem::path& source,
                             const std::filesystem::path& dep_file, const std::filesystem::path& dependency)
    -> std::optional<std::filesystem::path> {
    if (dependency.is_absolute()) {
        return exists_file(dependency) ? std::optional<std::filesystem::path>(dependency)
                                       : std::nullopt;
    }

    const std::filesystem::path project_root = source_root.parent_path();
    const std::vector<std::filesystem::path> candidates = {source.parent_path() / dependency,
                                              project_root / dependency,
                                              dep_file.parent_path() / dependency,
                                              dependency};
    for (const auto& candidate : candidates) {
        if (exists_file(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

}  // namespace

namespace build::fingerprint {

auto to_string(RebuildReason reason) -> const char* {
    switch (reason) {
        case RebuildReason::UpToDate:
            return "UpToDate";
        case RebuildReason::MissingObject:
            return "MissingObject";
        case RebuildReason::MissingDepFile:
            return "MissingDepFile";
        case RebuildReason::SourceNewer:
            return "SourceNewer";
        case RebuildReason::DependencyNewer:
            return "DependencyNewer";
        case RebuildReason::DependencyMissing:
            return "DependencyMissing";
        case RebuildReason::DepParseError:
            return "DepParseError";
    }
    return "Unknown";
}

auto evaluate_rebuild(const std::filesystem::path& source_root, const std::filesystem::path& source,
                      const std::filesystem::path& object_file, const std::filesystem::path& dep_file)
    -> RebuildDecision {
    if (!exists_file(object_file)) {
        return {.reason = RebuildReason::MissingObject, .dependency = {}};
    }
    if (!exists_file(dep_file)) {
        return {.reason = RebuildReason::MissingDepFile, .dependency = {}};
    }

    const auto object_time = read_time(object_file);
    const auto source_time = read_time(source);
    if (!object_time.has_value()) {
        return {.reason = RebuildReason::MissingObject, .dependency = {}};
    }
    if (!source_time.has_value() || *source_time > *object_time) {
        return {.reason = RebuildReason::SourceNewer, .dependency = source};
    }

    auto dependencies = depfile::parse_dependencies(dep_file);
    if (!dependencies) {
        return {.reason = RebuildReason::DepParseError, .dependency = {}};
    }

    for (const auto& dep : *dependencies) {
        auto resolved = resolve_dependency_path(source_root, source, dep_file, dep);
        if (!resolved.has_value()) {
            return {.reason = RebuildReason::DependencyMissing, .dependency = dep};
        }

        const auto dep_time = read_time(*resolved);
        if (!dep_time.has_value() || *dep_time > *object_time) {
            return {.reason = RebuildReason::DependencyNewer,
                    .dependency = *resolved};
        }
    }

    return {.reason = RebuildReason::UpToDate, .dependency = {}};
}

}  // namespace build::fingerprint


