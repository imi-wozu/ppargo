#include "build/manager_sources.hpp"

#include <algorithm>
#include <climits>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <span>

#include "build/graph.hpp"
#include "build/targets_internal.hpp"
#include "core/paths.hpp"
#include "util/text.hpp"

namespace build::detail {

namespace {

constexpr std::string_view kScanCacheHeader = "ppargo-source-scan-cache-v1";

struct CachedSourceEntry {
    std::string path;
    long long mtime = 0;
    std::uintmax_t size = 0;
};

struct ScanCacheData {
    std::string key;
    std::vector<std::pair<std::string, long long>> roots;
    std::vector<CachedSourceEntry> sources;
};

auto path_stamp_or_sentinel(const std::filesystem::path& path) -> long long {
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return LLONG_MIN;
    }
    return static_cast<long long>(stamp.time_since_epoch().count());
}

auto file_size_or_sentinel(const std::filesystem::path& path) -> std::uintmax_t {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return std::numeric_limits<std::uintmax_t>::max();
    }
    return size;
}

auto legacy_scan_cache_file_for(const std::filesystem::path& root)
    -> std::filesystem::path {
    return root / "target" / "cpp" / ".source_scan_cache";
}

auto scan_cache_file_for(const std::filesystem::path& root,
                         const core::Manifest& manifest,
                         const std::optional<std::filesystem::path>& output_dir_override)
    -> std::filesystem::path {
    return root / core::effective_output_dir(manifest, output_dir_override) /
           ".source_scan_cache";
}

void migrate_legacy_scan_cache_if_needed(const std::filesystem::path& cache_file,
                                         const std::filesystem::path& legacy_cache_file) {
    if (cache_file == legacy_cache_file) {
        return;
    }

    std::error_code ec;
    if (std::filesystem::exists(cache_file, ec) || ec) {
        return;
    }
    ec.clear();
    if (!std::filesystem::exists(legacy_cache_file, ec) || ec) {
        return;
    }

    std::filesystem::create_directories(cache_file.parent_path(), ec);
    if (ec) {
        return;
    }
    std::filesystem::copy_file(legacy_cache_file, cache_file,
                               std::filesystem::copy_options::skip_existing, ec);
}

auto append_sorted_paths(std::ostringstream& out, std::string_view section,
                         std::span<const std::filesystem::path>  paths) -> void {
    std::vector<std::string> normalized;
    normalized.reserve(paths.size());
    for (const auto& path : paths) {
        normalized.push_back(path.generic_string());
    }
    std::sort(normalized.begin(), normalized.end());

    out << section << "\n";
    for (const auto& path : normalized) {
        out << path << "\n";
    }
}

auto build_scan_cache_key(const core::Manifest& manifest,
                          const build::targets::ResolvedBuildTarget& resolved_target,
                          build::targets::SelectionKind selected_kind)
    -> std::string {
    std::ostringstream out;
    out << "source_dir=" << manifest.build.source_dir << "\n";
    out << "selection_kind=" << static_cast<int>(selected_kind) << "\n";
    out << "target_name=" << resolved_target.name << "\n";
    out << "binary_name=" << resolved_target.binary_name << "\n";

    std::vector<std::filesystem::path> excludes;
    excludes.reserve(manifest.build.exclude.size());
    for (const auto& item : manifest.build.exclude) {
        excludes.emplace_back(item);
    }
    append_sorted_paths(out, "[exclude]", excludes);
    append_sorted_paths(out, "[library_roots]", resolved_target.library_roots);
    append_sorted_paths(out, "[target_roots]", resolved_target.target_roots);
    append_sorted_paths(out, "[target_sources]", resolved_target.target_sources);

    return std::to_string(std::hash<std::string>{}(out.str()));
}

auto load_scan_cache(const std::filesystem::path& cache_file)
    -> std::optional<ScanCacheData> {
    std::error_code ec;
    if (!std::filesystem::exists(cache_file, ec) || ec) {
        return std::nullopt;
    }

    std::ifstream input(cache_file);
    if (!input.is_open()) {
        return std::nullopt;
    }

    std::string header;
    if (!std::getline(input, header) || header != kScanCacheHeader) {
        return std::nullopt;
    }

    ScanCacheData data;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        const auto parts = util::text::split_tab_fields(line);
        if (parts.empty()) {
            continue;
        }

        if (parts[0] == "key" && parts.size() >= 2) {
            data.key = parts[1];
            continue;
        }

        if (parts[0] == "root" && parts.size() >= 3) {
            std::istringstream mtime_in(parts[2]);
            long long mtime = 0;
            if (mtime_in >> mtime) {
                data.roots.emplace_back(parts[1], mtime);
            }
            continue;
        }

        if (parts[0] == "file" && parts.size() >= 4) {
            std::istringstream mtime_in(parts[2]);
            std::istringstream size_in(parts[3]);
            long long mtime = 0;
            std::uintmax_t size = 0;
            if ((mtime_in >> mtime) && (size_in >> size)) {
                data.sources.push_back(CachedSourceEntry{
                    .path = parts[1],
                    .mtime = mtime,
                    .size = size,
                });
            }
        }
    }

    if (data.key.empty()) {
        return std::nullopt;
    }
    return data;
}

auto write_scan_cache(const std::filesystem::path& cache_file, std::string_view  key,
                      std::span<const std::filesystem::path>  scan_roots,
                      std::span<const std::filesystem::path>  sources) -> void {
    std::error_code ec;
    std::filesystem::create_directories(cache_file.parent_path(), ec);
    if (ec) {
        return;
    }

    std::vector<std::string> root_strings;
    root_strings.reserve(scan_roots.size());
    for (const auto& root : scan_roots) {
        root_strings.push_back(root.generic_string());
    }
    std::sort(root_strings.begin(), root_strings.end());

    std::vector<std::string> source_strings;
    source_strings.reserve(sources.size());
    for (const auto& source : sources) {
        source_strings.push_back(source.generic_string());
    }
    std::sort(source_strings.begin(), source_strings.end());

    std::ofstream output(cache_file, std::ios::trunc);
    if (!output.is_open()) {
        return;
    }

    output << kScanCacheHeader << "\n";
    output << "key\t" << key << "\n";

    for (const auto& root : root_strings) {
        output << "root\t" << root << "\t"
               << path_stamp_or_sentinel(std::filesystem::path(root)) << "\n";
    }

    for (const auto& source : source_strings) {
        const auto source_path = std::filesystem::path(source);
        output << "file\t" << source << "\t" << path_stamp_or_sentinel(source_path)
               << "\t" << file_size_or_sentinel(source_path) << "\n";
    }
}

auto try_restore_cached_sources(const std::filesystem::path& cache_file,
                                std::string_view  key,
                                std::span<const std::filesystem::path>  scan_roots)
    -> std::optional<std::vector<std::filesystem::path>> {
    const auto loaded = load_scan_cache(cache_file);
    if (!loaded.has_value() || loaded->key != key) {
        return std::nullopt;
    }

    std::vector<std::string> expected_roots;
    expected_roots.reserve(scan_roots.size());
    for (const auto& root : scan_roots) {
        expected_roots.push_back(root.generic_string());
    }
    std::sort(expected_roots.begin(), expected_roots.end());

    std::vector<std::pair<std::string, long long>> actual_roots;
    actual_roots.reserve(expected_roots.size());
    for (const auto& root : expected_roots) {
        actual_roots.emplace_back(root,
                                  path_stamp_or_sentinel(std::filesystem::path(root)));
    }

    auto cached_roots = loaded->roots;
    std::sort(cached_roots.begin(), cached_roots.end(), [](const auto& lhs,
                                                            const auto& rhs) {
        return lhs.first < rhs.first;
    });

    if (cached_roots != actual_roots) {
        return std::nullopt;
    }

    std::vector<std::filesystem::path> sources;
    sources.reserve(loaded->sources.size());
    for (const auto& source : loaded->sources) {
        const auto source_path = std::filesystem::path(source.path);
        if (path_stamp_or_sentinel(source_path) != source.mtime) {
            return std::nullopt;
        }
        if (file_size_or_sentinel(source_path) != source.size) {
            return std::nullopt;
        }
        sources.push_back(source_path);
    }

    std::sort(sources.begin(), sources.end());
    if (sources.empty()) {
        return std::nullopt;
    }
    return sources;
}

auto collect_recursive_sources(const std::filesystem::path& root,
                               const std::filesystem::path& base,
                               const build::graph::CompiledExcludes& excludes,
                               std::set<std::string>& seen,
                               std::vector<std::filesystem::path>& out,
                               bool skip_source_main, bool skip_source_bins,
                               const std::filesystem::path& canonical_source_root)
    -> util::Status {
    std::error_code ec;
    if (!std::filesystem::exists(base, ec)) {
        if (ec) {
            return std::unexpected(util::make_error("Failed to access source path: " +
                                       base.string()));
        }
        return util::Ok;
    }

    for (std::filesystem::recursive_directory_iterator it(base, ec), end; it != end;
         it.increment(ec)) {
        if (ec) {
            return std::unexpected(util::make_error("Failed while scanning source path: " + base.string()));
        }

        if (!it->is_regular_file(ec)) {
            if (ec) {
                return std::unexpected(util::make_error("Failed to inspect source entry: " +
                    it->path().string()));
            }
            continue;
        }

        const auto canonical_source = std::filesystem::weakly_canonical(it->path(), ec);
        if (ec) {
            return std::unexpected(util::make_error("Failed to canonicalize source file: " +
                it->path().string()));
        }
        if (!build::targets::detail::is_cpp_source_path(canonical_source)) {
            continue;
        }

        const auto relative_to_root = std::filesystem::relative(canonical_source, root, ec);
        if (ec) {
            return std::unexpected(util::make_error("Failed to resolve source path: " +
                canonical_source.string()));
        }
        const auto rel_root_str = relative_to_root.generic_string();
        if (build::graph::matches_excludes(rel_root_str, excludes)) {
            continue;
        }

        if (skip_source_main || skip_source_bins) {
            const auto relative_to_source =
                std::filesystem::relative(canonical_source, canonical_source_root, ec);
            if (!ec) {
                const auto rel = relative_to_source.generic_string();
                if (skip_source_main && rel == "main.cpp") {
                    continue;
                }
                if (skip_source_bins && rel.rfind("bin/", 0) == 0) {
                    continue;
                }
            }
            ec.clear();
        }

        if (seen.insert(canonical_source.generic_string()).second) {
            out.push_back(canonical_source);
        }
    }

    return util::Ok;
}

auto add_explicit_source(const std::filesystem::path& root,
                         const std::filesystem::path& source,
                         const build::graph::CompiledExcludes& excludes,
                         std::set<std::string>& seen,
                         std::vector<std::filesystem::path>& out) -> util::Status {
    std::error_code ec;
    const auto canonical_source = std::filesystem::weakly_canonical(source, ec);
    if (ec) {
        return std::unexpected(util::make_error("Failed to canonicalize source file: " +
                                   source.string()));
    }
    const auto relative_to_root = std::filesystem::relative(canonical_source, root, ec);
    if (ec) {
        return std::unexpected(util::make_error("Failed to resolve source path: " +
                                   canonical_source.string()));
    }
    if (build::graph::matches_excludes(relative_to_root.generic_string(), excludes)) {
        return util::Ok;
    }
    if (seen.insert(canonical_source.generic_string()).second) {
        out.push_back(canonical_source);
    }
    return util::Ok;
}

}  // namespace

auto collect_target_sources(const std::filesystem::path& root,
                            const core::Manifest& manifest,
                            const build::targets::ResolvedBuildTarget& resolved_target,
                            build::targets::SelectionKind selected_kind,
                            const std::optional<std::filesystem::path>& output_dir_override)
    -> util::Result<CollectedSources> {
    const auto compiled_excludes =
        build::graph::compile_excludes(manifest.build.exclude);

    std::error_code ec;
    const auto source_root =
        std::filesystem::weakly_canonical(root / manifest.build.source_dir, ec);
    if (ec) {
        return std::unexpected(util::make_error("Failed to canonicalize source directory: " +
            (root / manifest.build.source_dir).string()));
    }

    std::vector<std::filesystem::path> scan_roots;
    scan_roots.push_back(source_root);

    for (const auto& library_root : resolved_target.library_roots) {
        const auto canonical_library = std::filesystem::weakly_canonical(library_root, ec);
        if (ec) {
            return std::unexpected(util::make_error("Failed to canonicalize source path: " +
                library_root.string()));
        }
        scan_roots.push_back(canonical_library);
    }

    for (const auto& target_root : resolved_target.target_roots) {
        const auto canonical_target = std::filesystem::weakly_canonical(target_root, ec);
        if (ec) {
            return std::unexpected(util::make_error("Failed to canonicalize source path: " +
                target_root.string()));
        }
        scan_roots.push_back(canonical_target);
    }

    std::sort(scan_roots.begin(), scan_roots.end());
    scan_roots.erase(std::unique(scan_roots.begin(), scan_roots.end()),
                     scan_roots.end());

    const auto cache_key =
        build_scan_cache_key(manifest, resolved_target, selected_kind);
    const auto cache_file = scan_cache_file_for(root, manifest, output_dir_override);
    migrate_legacy_scan_cache_if_needed(cache_file, legacy_scan_cache_file_for(root));
    if (const auto cached_sources =
            try_restore_cached_sources(cache_file, cache_key, scan_roots);
        cached_sources.has_value()) {
        return CollectedSources{
            .source_root = source_root,
            .sources = *cached_sources,
        };
    }

    std::set<std::string> seen_sources;
    std::vector<std::filesystem::path> sources;

    for (const auto& library_root : resolved_target.library_roots) {
        const auto canonical_library = std::filesystem::weakly_canonical(library_root, ec);
        if (ec) {
            return std::unexpected(util::make_error("Failed to canonicalize source path: " +
                library_root.string()));
        }
        const bool is_source_root = canonical_library == source_root;
        const bool skip_main_for_this_root =
            selected_kind != build::targets::SelectionKind::DefaultBin && is_source_root;
        const bool skip_bins_for_this_root = is_source_root;
        GUARD(collect_recursive_sources(root, canonical_library, compiled_excludes,
                                           seen_sources, sources, skip_main_for_this_root,
                                           skip_bins_for_this_root, source_root));
    }

    for (const auto& target_root : resolved_target.target_roots) {
        GUARD(collect_recursive_sources(root, target_root, compiled_excludes,
                                           seen_sources, sources, false, false,
                                           source_root));
    }

    for (const auto& source : resolved_target.target_sources) {
        GUARD(add_explicit_source(root, source, compiled_excludes, seen_sources,
                                     sources));
    }

    if (sources.empty()) {
        return std::unexpected(util::make_error("No source files found for selected target."));
    }

    std::sort(sources.begin(), sources.end());
    write_scan_cache(cache_file, cache_key, scan_roots, sources);

    return CollectedSources{.source_root = source_root, .sources = std::move(sources)};
}

}  // namespace build::detail








