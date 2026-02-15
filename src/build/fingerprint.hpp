#pragma once

#include <filesystem>


namespace build::fingerprint {

enum class RebuildReason {
    UpToDate,
    MissingObject,
    MissingDepFile,
    SourceNewer,
    DependencyNewer,
    DependencyMissing,
    DepParseError,
};

struct RebuildDecision {
    RebuildReason reason = RebuildReason::UpToDate;
    std::filesystem::path dependency;
};

auto to_string(RebuildReason reason) -> const char*;

auto evaluate_rebuild(const std::filesystem::path& source_root,
                      const std::filesystem::path& source,
                      const std::filesystem::path& object_file,
                      const std::filesystem::path& dep_file) -> RebuildDecision;

}  // namespace build::fingerprint



