#include "build/compile.hpp"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <format>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "build/fingerprint.hpp"
#include "build/graph.hpp"
#include "build/process_bridge.hpp"
#include "core/paths.hpp"
#include "util/fs.hpp"
#include "util/output.hpp"


namespace {

struct CheckCacheEntry {
    long long mtime = 0;
    std::uintmax_t size = 0;
};

auto normalize_path(const std::filesystem::path& path) -> std::string {
    return path.generic_string();
}

auto resolved_jobs() -> int {
    const unsigned hw = std::thread::hardware_concurrency();
    int jobs = hw == 0 ? 1 : static_cast<int>(hw);
    if (jobs < 1) {
        jobs = 1;
    }
    if (jobs > 64) {
        jobs = 64;
    }
    return jobs;
}

template <typename Fn>
auto run_parallel(std::size_t jobs, std::size_t count, Fn&& fn) -> util::Status {
    if (count == 0) {
        return util::Ok;
    }

    const std::size_t worker_count = std::max<std::size_t>(1, std::min(jobs, count));
    std::atomic<std::size_t> next{0};
    std::atomic<bool> stop{false};
    std::optional<std::string> first_error;
    std::mutex error_mutex;

    auto worker = [&stop, &next, count, &fn, &first_error, &error_mutex]() {
        while (true) {
            if (stop.load(std::memory_order_relaxed)) {
                return;
            }

            const std::size_t index = next.fetch_add(1);
            if (index >= count) {
                return;
            }

            auto status = fn(index);
            if (!status) {
                std::lock_guard<std::mutex> lock(error_mutex);
                if (!first_error.has_value()) {
                    first_error = status.error();
                }
                stop.store(true, std::memory_order_relaxed);
                return;
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(worker_count > 0 ? worker_count - 1 : 0);
    for (std::size_t i = 1; i < worker_count; ++i) {
        workers.emplace_back(worker);
    }
    worker();

    for (auto& t : workers) {
        if (t.joinable()) {
            t.join();
        }
    }

    if (first_error.has_value()) {
        return std::unexpected(*first_error);
    }
    return util::Ok;
}

auto compile_flags(const core::Manifest& manifest, bool release)
    -> std::vector<std::string> {
    std::vector<std::string> flags;
    if (manifest.package.edition == "cpp26") {
        flags.push_back("-std=c++26");
    } else if (manifest.package.edition == "cpp23") {
        flags.push_back("-std=c++23");
    } else if (manifest.package.edition == "cpp20") {
        flags.push_back("-std=c++20");
    } else {
        flags.push_back("-std=c++17");
    }

    if (release) {
        flags.push_back("-O3");
    } else {
        flags.push_back("-O0");
        flags.push_back("-g");
    }

#ifdef _WIN32
    flags.push_back("-fms-runtime-lib=dll");
#endif

    flags.push_back("-Wall");
    flags.push_back("-Wextra");
    return flags;
}

auto include_paths_for_manifest(const std::filesystem::path& root, const core::Manifest& manifest)
    -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> include_paths;
    include_paths.reserve(manifest.build.include_dirs.size() + 1);
    for (const auto& include_dir : manifest.build.include_dirs) {
        include_paths.push_back(root / include_dir);
    }
    include_paths.push_back(root / "packages" / core::detect_triplet() / "include");
    return include_paths;
}

auto compiler_for_manifest(const core::Manifest& manifest) -> std::filesystem::path {
    return manifest.toolchain.compiler.empty() ? std::filesystem::path("clang++")
                                               : std::filesystem::path(manifest.toolchain.compiler);
}

auto compile_source(const build::compile::CompilerConfig& config,
                    const std::filesystem::path& source, const std::filesystem::path& object_file,
                    const std::filesystem::path& dep_file) -> util::Status {
    std::vector<std::string> args;
    args.reserve(config.flags.size() + config.include_paths.size() * 2 + 8);
    for (const auto& flag : config.flags) {
        args.push_back(flag);
    }
    for (const auto& include_path : config.include_paths) {
        args.push_back("-I");
        args.push_back(include_path.string());
    }
    args.push_back("-c");
    args.push_back(source.string());
    args.push_back("-o");
    args.push_back(object_file.string());
    args.push_back("-MMD");
    args.push_back("-MF");
    args.push_back(dep_file.string());

    auto rc = TRY(build::process_bridge::run(config.compiler, args));
    if (rc != 0) {
        return std::unexpected("Build Error: Compilation failed for: " +
                               source.string());
    }
    return util::Ok;
}

auto check_source(const build::compile::CompilerConfig& config,
                  const std::filesystem::path& source) -> util::Status {
    std::vector<std::string> args;
    args.reserve(config.flags.size() + config.include_paths.size() * 2 + 3);
    for (const auto& flag : config.flags) {
        args.push_back(flag);
    }
    for (const auto& include_path : config.include_paths) {
        args.push_back("-I");
        args.push_back(include_path.string());
    }
    args.push_back("-fsyntax-only");
    args.push_back(source.string());

    auto rc = TRY(build::process_bridge::run(config.compiler, args));
    if (rc != 0) {
        return std::unexpected("Build Error: Check failed for: " + source.string());
    }
    return util::Ok;
}

auto recreate_directory(const std::filesystem::path& dir) -> util::Status {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (ec) {
        return std::unexpected("I/O Error: Failed to remove directory: " +
                               dir.string() + " (" + ec.message() + ")");
    }

    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return std::unexpected("I/O Error: Failed to create directory: " +
                               dir.string() + " (" + ec.message() + ")");
    }
    return util::Ok;
}

auto file_stamp(const std::filesystem::path& path) -> util::Result<long long> {
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::unexpected("I/O Error: Failed to read timestamp: " +
                               path.string() + " (" + ec.message() + ")");
    }
    return static_cast<long long>(stamp.time_since_epoch().count());
}

auto file_size_value(const std::filesystem::path& path) -> util::Result<std::uintmax_t> {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return std::unexpected("I/O Error: Failed to read file size: " +
                               path.string() + " (" + ec.message() + ")");
    }
    return size;
}

auto signature_hash_text(const std::string& signature) -> std::string {
    return std::to_string(std::hash<std::string>{}(signature));
}

auto incremental_trace_enabled() -> bool {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&value, &len, "PPARGO_TRACE_INCREMENTAL") != 0 ||
        value == nullptr) {
        return false;
    }
    const bool enabled = len > 0;
    std::free(value);
    return enabled;
#else
    return std::getenv("PPARGO_TRACE_INCREMENTAL") != nullptr;
#endif
}

auto load_check_cache(const std::filesystem::path& cache_file, const std::string& signature_hash)
    -> std::unordered_map<std::string, CheckCacheEntry> {
    std::unordered_map<std::string, CheckCacheEntry> cache;
    std::error_code ec;
    if (!std::filesystem::exists(cache_file, ec) || ec) {
        return cache;
    }

    std::ifstream input(cache_file);
    if (!input.is_open()) {
        return cache;
    }

    std::string line;
    if (!std::getline(input, line) || line != ("sig=" + signature_hash)) {
        return cache;
    }

    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        const auto first_tab = line.find('\t');
        const auto second_tab = line.find(
            '\t', first_tab == std::string::npos ? 0 : first_tab + 1);
        if (first_tab == std::string::npos || second_tab == std::string::npos) {
            continue;
        }

        const std::string path = line.substr(0, first_tab);
        const std::string mtime_text =
            line.substr(first_tab + 1, second_tab - first_tab - 1);
        const std::string size_text = line.substr(second_tab + 1);

        auto mtime_stream = std::istringstream(mtime_text);
        auto size_stream = std::istringstream(size_text);
        long long mtime = 0;
        std::uintmax_t size = 0;
        if ((mtime_stream >> mtime) && (size_stream >> size)) {
            cache[path] = CheckCacheEntry{mtime, size};
        }
    }

    return cache;
}

auto write_check_cache(
    const std::filesystem::path& cache_file, const std::string& signature_hash,
    const std::unordered_map<std::string, CheckCacheEntry>& cache) -> util::Status {
    std::ostringstream out;
    out << "sig=" << signature_hash << "\n";
    for (const auto& [path, entry] : cache) {
        out << path << "\t" << entry.mtime << "\t" << entry.size << "\n";
    }
    return util::fs::atomic_write_text_result(cache_file, out.str());
}

}  // namespace

namespace build::compile {

auto make_compiler_config(const std::filesystem::path& root, const core::Manifest& manifest,
                          bool release) -> util::Result<CompilerConfig> {
    CompilerConfig config;
    config.compiler = compiler_for_manifest(manifest);
    config.flags = compile_flags(manifest, release);
    config.include_paths = include_paths_for_manifest(root, manifest);
    config.jobs = resolved_jobs();
    return config;
}

auto compile_signature(const CompilerConfig& config) -> std::string {
    std::ostringstream out;
    out << config.compiler.generic_string() << "\n";
    for (const auto& flag : config.flags) {
        out << flag << "\n";
    }
    out << "--include-paths--\n";
    for (const auto& include_path : config.include_paths) {
        out << include_path.generic_string() << "\n";
    }
    return out.str();
}

auto refresh_object_dir_if_signature_changed(const std::filesystem::path& build_root,
                                             const std::string& signature)
    -> util::Status {
    const std::filesystem::path signature_file = build_root / ".compile_signature";
    bool changed = true;

    std::error_code ec;
    if (std::filesystem::exists(signature_file, ec) && !ec) {
        std::ifstream input(signature_file);
        if (!input.is_open()) {
            return std::unexpected("I/O Error: Failed to open signature file: " +
                                   signature_file.string());
        }
        std::stringstream buffer;
        buffer << input.rdbuf();
        changed = buffer.str() != signature;
    }

    if (changed) {
        TRY_void(recreate_directory(build_root / "obj"));
        TRY_void(util::fs::atomic_write_text_result(signature_file, signature));
    }
    return util::Ok;
}

auto compile_objects(const std::filesystem::path& source_root, const std::filesystem::path& obj_root,
                     const std::vector<std::filesystem::path>& sources,
                     const CompilerConfig& config, bool force_rebuild)
    -> util::Result<CompileResult> {
    struct CompileTask {
        std::filesystem::path source;
        std::filesystem::path object_file;
        std::filesystem::path dep_file;
    };

    CompileResult result;
    result.objects.reserve(sources.size());

    std::vector<CompileTask> tasks;
    tasks.reserve(sources.size());
    const bool trace = incremental_trace_enabled();

    for (const auto& source : sources) {
        auto object_file =
            TRY(build::graph::object_path_for_source(source_root, obj_root, source));

        std::error_code ec;
        std::filesystem::create_directories(object_file.parent_path(), ec);
        if (ec) {
            return std::unexpected("I/O Error: Failed to create object directory: " +
                                   object_file.parent_path().string() + " (" +
                                   ec.message() + ")");
        }

        auto dep_file = object_file;
        dep_file.replace_extension(".d");

        const auto decision = force_rebuild
                                  ? build::fingerprint::RebuildDecision{}
                                  : build::fingerprint::evaluate_rebuild(
                                        source_root, source, object_file, dep_file);
        if (force_rebuild ||
            decision.reason != build::fingerprint::RebuildReason::UpToDate) {
            tasks.push_back(CompileTask{source, object_file, dep_file});
            if (trace) {
                if (force_rebuild) {
                    util::output::trace(std::format(
                        "incremental: {} [ForceRebuild]", source.string()));
                } else if (decision.dependency.empty()) {
                    util::output::trace(std::format(
                        "incremental: {} [{}]", source.string(),
                        build::fingerprint::to_string(decision.reason)));
                } else {
                    util::output::trace(std::format(
                        "incremental: {} [{}: {}]", source.string(),
                        build::fingerprint::to_string(decision.reason),
                        decision.dependency.string()));
                }
            }
        }

        result.objects.push_back(object_file);
    }

    TRY_void(run_parallel(
        static_cast<std::size_t>(config.jobs), tasks.size(),
        [&config, &tasks](std::size_t i) {
            return compile_source(config, tasks[i].source, tasks[i].object_file,
                                  tasks[i].dep_file);
        }));

    result.compiled_count = tasks.size();
    return result;
}

auto run_checks_with_cache(const std::filesystem::path& root, const std::filesystem::path& build_root,
                           const std::vector<std::filesystem::path>& sources,
                           const CompilerConfig& config) -> util::Status {
    struct CheckTask {
        std::filesystem::path source;
        std::string relative_path;
        long long mtime = 0;
        std::uintmax_t size = 0;
    };

    const auto signature_hash = signature_hash_text(compile_signature(config));
    const auto cache_file = build_root / ".check_cache";
    auto cache = load_check_cache(cache_file, signature_hash);

    std::vector<CheckTask> stale;
    stale.reserve(sources.size());

    for (const auto& source : sources) {
        std::error_code ec;
        const auto relative_path = std::filesystem::relative(source, root, ec);
        if (ec) {
            return std::unexpected("Build Error: Failed to resolve relative source path: " +
                                   source.string());
        }
        const std::string relative = normalize_path(relative_path);
        auto mtime = TRY(file_stamp(source));
        auto size = TRY(file_size_value(source));

        const auto found = cache.find(relative);
        if (found != cache.end() && found->second.mtime == mtime &&
            found->second.size == size) {
            continue;
        }

        stale.push_back(CheckTask{source, relative, mtime, size});
    }

    TRY_void(run_parallel(
        static_cast<std::size_t>(config.jobs), stale.size(),
        [&config, &stale](std::size_t i) { return check_source(config, stale[i].source); }));

    for (const auto& task : stale) {
        cache[task.relative_path] = CheckCacheEntry{task.mtime, task.size};
    }

    return write_check_cache(cache_file, signature_hash, cache);
}

}  // namespace build::compile


