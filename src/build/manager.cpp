#include "build/manager.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>

#include "build/compile.hpp"
#include "build/link.hpp"
#include "build/source_scan.hpp"
#include "core/paths.hpp"
#include "util/fs.hpp"
#include "util/output.hpp"


namespace build {

namespace {

auto read_text_file(const std::filesystem::path& path) -> std::optional<std::string> {
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

auto make_link_signature(const compile::CompilerConfig& config, const std::filesystem::path& output,
                         const std::vector<std::filesystem::path>& library_paths,
                         const std::vector<std::string>& libraries,
                         bool release) -> std::string {
    std::ostringstream out;
    out << config.compiler.generic_string() << "\n";
    out << "profile=" << (release ? "release" : "debug") << "\n";
    out << "output=" << output.generic_string() << "\n";
    out << "flags=\n";
    for (const auto& flag : config.flags) {
        out << flag << "\n";
    }
    out << "linker_flags=\n";
    out << "-fuse-ld=lld\n";
#ifdef _WIN32
    out << "/defaultlib:msvcrt\n";
    out << "/nodefaultlib:libcmt\n";
    out << "/nodefaultlib:libcmtd\n";
#else
    if (release) {
        out << "-s\n";
    }
#endif
    out << "library_paths=\n";
    for (const auto& path : library_paths) {
        out << path.generic_string() << "\n";
    }
    out << "libraries=\n";
    for (const auto& library : libraries) {
        out << library << "\n";
    }
    return out.str();
}

auto resolve_library_artifact(const std::vector<std::filesystem::path>& library_paths,
                              const std::string& library) -> std::optional<std::filesystem::path> {
    for (const auto& base : library_paths) {
#ifdef _WIN32
        const std::vector<std::filesystem::path> candidates = {base / (library + ".lib"),
                                                  base / ("lib" + library + ".lib")};
#else
        const std::vector<std::filesystem::path> candidates = {base / ("lib" + library + ".a"),
                                                  base / ("lib" + library + ".so")};
#endif
        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && !ec) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

auto needs_link(const std::vector<std::filesystem::path>& objects, const std::filesystem::path& output,
                std::size_t compiled_count, const std::vector<std::filesystem::path>& library_paths,
                const std::vector<std::string>& libraries,
                const std::string& link_signature,
                const std::filesystem::path& link_signature_file) -> bool {
    std::error_code ec;
    if (compiled_count > 0 || !std::filesystem::exists(output, ec) || ec) {
        return true;
    }

    const auto output_time = std::filesystem::last_write_time(output, ec);
    if (ec) {
        return true;
    }

    for (const auto& object : objects) {
        if (!std::filesystem::exists(object, ec) || ec) {
            return true;
        }
        const auto object_time = std::filesystem::last_write_time(object, ec);
        if (ec || object_time > output_time) {
            return true;
        }
    }

    for (const auto& library : libraries) {
        const auto resolved = resolve_library_artifact(library_paths, library);
        if (!resolved.has_value()) {
            return true;
        }
        const auto lib_time = std::filesystem::last_write_time(*resolved, ec);
        if (ec || lib_time > output_time) {
            return true;
        }
    }

    if (!std::filesystem::exists(link_signature_file, ec) || ec) {
        return true;
    }
    const auto stored_signature = read_text_file(link_signature_file);
    if (!stored_signature.has_value()) {
        return true;
    }
    if (*stored_signature != link_signature) {
        return true;
    }

    return false;
}

}  // namespace

auto execute(const std::filesystem::path& root, const core::Manifest& manifest,
             const BuildOptions& options) -> util::Result<BuildExecutionResult> {
    BuildExecutionResult result;

    auto sources = TRY(source_scan::collect_sources(root, manifest));
    if (sources.empty()) {
        return std::unexpected("Build Error: No source files found.");
    }

    const std::filesystem::path build_root = core::build_dir(root, manifest, options.release);
    std::error_code ec;
    std::filesystem::create_directories(build_root, ec);
    if (ec) {
        return std::unexpected("I/O Error: Failed to create build directory: " +
                               build_root.string() + " (" + ec.message() + ")");
    }

    auto config = TRY(compile::make_compiler_config(root, manifest, options.release));

    if (options.mode == BuildOptions::Mode::Check) {
        TRY_void(compile::run_checks_with_cache(root, build_root, sources, config));
        return result;
    }

    const std::filesystem::path source_root = root / manifest.build.source_dir;
    const std::filesystem::path obj_root = build_root / "obj";
    std::filesystem::create_directories(obj_root, ec);
    if (ec) {
        return std::unexpected("I/O Error: Failed to create object directory: " +
                               obj_root.string() + " (" + ec.message() + ")");
    }

    const auto signature = compile::compile_signature(config);
    TRY_void(compile::refresh_object_dir_if_signature_changed(build_root, signature));

    auto compile_result =
        TRY(compile::compile_objects(source_root, obj_root, sources, config, false));
    result.compiled_units = compile_result.compiled_count;

    std::vector<std::filesystem::path> library_paths = {
        root / "packages" / core::detect_triplet() / "lib"};
    std::vector<std::string> libraries;
    libraries.reserve(manifest.dependencies.size());
    for (const auto& [name, _] : manifest.dependencies) {
        libraries.push_back(name);
    }

    const std::filesystem::path output = build_root / core::binary_name(manifest);
    const std::filesystem::path link_signature_file = build_root / ".link_signature";
    const auto link_signature =
        make_link_signature(config, output, library_paths, libraries, options.release);
    TRY_void(link::ensure_not_running_target(output));

    if (needs_link(compile_result.objects, output, compile_result.compiled_count,
                   library_paths, libraries, link_signature, link_signature_file)) {
        auto first_link = link::link_binary(config, compile_result.objects, output,
                                            library_paths, libraries,
                                            options.release);
        if (first_link) {
            result.linked = true;
        } else {
            util::output::warning(
                "Incremental link failed. Retrying with a clean object rebuild.");
            std::filesystem::remove_all(obj_root, ec);
            std::filesystem::create_directories(obj_root, ec);
            if (ec) {
                return std::unexpected(
                    "I/O Error: Failed to recreate object directory: " +
                    obj_root.string() + " (" + ec.message() + ")");
            }

            auto compile_retry_result =
                TRY(compile::compile_objects(source_root, obj_root, sources, config, true));
            compile_result = std::move(compile_retry_result);
            result.compiled_units += compile_result.compiled_count;

            auto second_link = link::link_binary(config, compile_result.objects,
                                                 output, library_paths, libraries,
                                                 options.release);
            if (second_link) {
                result.linked = true;
            } else {
                return std::unexpected(first_link.error() +
                                       " Retry after clean rebuild failed: " +
                                       second_link.error());
            }
        }
    }

    if (result.linked) {
        TRY_void(
            util::fs::atomic_write_text_result(link_signature_file, link_signature));
        TRY_void(link::copy_runtime_dlls(root, build_root));
    }

    return result;
}

auto build(const std::filesystem::path& root, const core::Manifest& manifest,
           bool release) -> util::Status {
    BuildOptions options;
    options.release = release;
    options.mode = BuildOptions::Mode::Build;
    auto ignored = TRY(execute(root, manifest, options));
    (void)ignored;
    return util::Ok;
}

auto check(const std::filesystem::path& root, const core::Manifest& manifest,
           bool release) -> util::Status {
    BuildOptions options;
    options.release = release;
    options.mode = BuildOptions::Mode::Check;
    auto ignored = TRY(execute(root, manifest, options));
    (void)ignored;
    return util::Ok;
}

}  // namespace build


