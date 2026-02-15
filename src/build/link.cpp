#include "build/link.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "build/process_bridge.hpp"
#include "core/paths.hpp"


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

#ifdef _WIN32
auto current_executable_path() {
    std::vector<char> buffer(MAX_PATH);
    DWORD size =
        GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0) {
        return std::optional<std::filesystem::path>{};
    }
    while (size == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        size =
            GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0) {
            return std::optional<std::filesystem::path>{};
        }
    }
    return std::optional<std::filesystem::path>{std::filesystem::path(std::string(buffer.data(), size))};
}
#endif

}  // namespace

namespace build::link {

auto ensure_not_running_target(const std::filesystem::path& output) -> util::Status {
#ifdef _WIN32
    if (const auto exe = current_executable_path(); exe.has_value()) {
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
            return std::unexpected(
                "Build Error: Cannot overwrite currently running executable on Windows. "
                "Build the other profile (debug/release) instead.");
        }
    }
#else
    (void)output;
#endif
    return util::Ok;
}

auto link_binary(const compile::CompilerConfig& config,
                 const std::vector<std::filesystem::path>& object_files, const std::filesystem::path& output,
                 const std::vector<std::filesystem::path>& library_paths,
                 const std::vector<std::string>& libraries,
                 bool release) -> util::Status {
    const std::filesystem::path rsp_file = output.parent_path() / "link_objects.rsp";
    std::ofstream rsp(rsp_file, std::ios::trunc);
    if (!rsp.is_open()) {
        return std::unexpected(
            "I/O Error: Failed to create linker response file.");
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

    auto rc = build::process_bridge::run(config.compiler, args);
    if (!rc) {
        return std::unexpected(rc.error());
    }
    std::error_code ec;
    std::filesystem::remove(rsp_file, ec);
    if (*rc != 0) {
        return std::unexpected("Build Error: Linking failed for output: " +
                               output.string());
    }
    return util::Ok;
}

auto copy_runtime_dlls(const std::filesystem::path& root, const std::filesystem::path& build_dir)
    -> util::Status {
#ifdef _WIN32
    const std::filesystem::path vcpkg_bin = root / "packages" / core::detect_triplet() / "bin";
    std::error_code ec;
    if (!std::filesystem::exists(vcpkg_bin, ec) || ec) {
        return util::Ok;
    }

    for (std::filesystem::directory_iterator it(vcpkg_bin, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            return std::unexpected("I/O Error: Failed to enumerate runtime DLL directory: " +
                                   vcpkg_bin.string());
        }
        if (!it->is_regular_file(ec)) {
            if (ec) {
                return std::unexpected("I/O Error: Failed to inspect runtime DLL entry: " +
                                       it->path().string());
            }
            continue;
        }
        if (it->path().extension() != ".dll") {
            continue;
        }
        const std::filesystem::path dest = build_dir / it->path().filename();
        if (should_copy(it->path(), dest)) {
            std::filesystem::copy_file(it->path(), dest, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                return std::unexpected("I/O Error: Failed to copy runtime DLL: " +
                                       it->path().string());
            }
        }
    }
#else
    (void)root;
    (void)build_dir;
#endif
    return util::Ok;
}

}  // namespace build::link


