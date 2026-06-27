#include "build/link.hpp"

#include <fstream>
#include <optional>
#include <span>
#include <vector>

#include "build/process_bridge.hpp"
#include "core/system.hpp"

namespace {

auto should_copy(const std::filesystem::path& src, const std::filesystem::path& dest) {
    std::error_code ec;
    if (!std::filesystem::exists(dest, ec) || ec) {
        return true;
    }
    const auto src_time = std::filesystem::last_write_time(src, ec);
    if (ec) {
        return true;
    }
    const auto dest_time = std::filesystem::last_write_time(dest, ec);
    if (ec) {
        return true;
    }
    return src_time > dest_time;
}

}  // namespace

namespace build::link {

auto ensure_not_running_target(const std::filesystem::path& output) -> util::Status {
#ifdef _WIN32
    if (const auto exe = core::current_executable_path(); exe.has_value()) {
        std::error_code ec;
        const auto current = std::filesystem::weakly_canonical(*exe, ec);
        if (ec) {
            return util::Ok;
        }
        const auto target = std::filesystem::weakly_canonical(output, ec);
        if (ec) {
            return util::Ok;
        }
        if (!current.empty() && current == target) {
            return std::unexpected(util::make_error(
                "Cannot overwrite currently running executable on Windows. "
                "Build the other profile (debug/release) instead."));
        }
    }
#else
    (void)output;
#endif
    return util::Ok;
}

auto link_binary_result(const compile::CompilerConfig& config,
                        std::span<const std::filesystem::path>  object_files,
                        const std::filesystem::path& output,
                        std::span<const std::filesystem::path>  library_paths,
                        std::span<const std::string>  libraries, bool release,
                        const std::atomic_bool* cancel_requested)
    -> util::Result<LinkExecutionResult> {
    const std::filesystem::path rsp_file = output.parent_path() / "link_objects.rsp";
    std::ofstream rsp(rsp_file, std::ios::trunc);
    if (!rsp.is_open()) {
        return std::unexpected(
            util::make_error("Failed to create linker response file."));
    }
    for (const auto& object_file : object_files) {
        rsp << "\"" << object_file.generic_string() << "\"\n";
    }
    rsp.close();

    std::vector<std::string> args;
    args.push_back("-fuse-ld=lld");
#ifdef _WIN32
    args.push_back("-Xlinker");
    args.push_back("/defaultlib:msvcrt");
    args.push_back("-Xlinker");
    args.push_back("/nodefaultlib:libcmt");
    args.push_back("-Xlinker");
    args.push_back("/nodefaultlib:libcmtd");
    args.push_back("-Xlinker");
    args.push_back("/subsystem:console");
#endif
    args.push_back("@" + rsp_file.generic_string());
    args.push_back("-o");
    args.push_back(output.string());
    for (const auto& path : library_paths) {
        args.push_back("-L");
        args.push_back(path.string());
    }
    for (const auto& library : libraries) {
        args.push_back("-l" + library);
    }
    if (release) {
#ifndef _WIN32
        args.push_back("-s");
#endif
    }

    auto run = build::process_bridge::run(config.compiler, args, cancel_requested);
    if (!run) {
        return std::unexpected(run.error());
    }
    std::error_code ec;
    std::filesystem::remove(rsp_file, ec);
    return LinkExecutionResult{
        .exit_code = run->exit_code,
        .output = std::move(run->output),
        .canceled = run->canceled,
    };
}

auto copy_runtime_dlls(std::span<const std::filesystem::path> runtime_files,
                       const std::filesystem::path& build_dir) -> util::Status {
#ifdef _WIN32
    std::error_code ec;
    for (const auto& runtime_file : runtime_files) {
        if (!std::filesystem::exists(runtime_file, ec) || ec) {
            if (ec) {
                return std::unexpected(util::make_error(
                    "Failed to inspect runtime DLL: " + runtime_file.string() +
                    " (" + ec.message() + ")"));
            }
            continue;
        }
        if (runtime_file.extension() != ".dll") {
            continue;
        }
        const std::filesystem::path dest = build_dir / runtime_file.filename();
        if (should_copy(runtime_file, dest)) {
            std::filesystem::copy_file(
                runtime_file, dest,
                std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                return std::unexpected(util::make_error(
                    "Failed to copy runtime DLL: " + runtime_file.string()));
            }
        }
    }
#else
    (void)runtime_files;
    (void)build_dir;
#endif
    return util::Ok;
}

}  // namespace build::link


