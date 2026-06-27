#include "util/fs.hpp"

#include <atomic>
#include <chrono>
#include <format>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace util::fs {

namespace {

auto io_error(std::string_view action, const std::filesystem::path& path,
              const std::error_code& error) -> util::Status {
    return std::unexpected(util::make_error(
        std::format("Failed to {}: {} ({})", action, path.string(),
                    error.message())));
}

auto process_id() -> unsigned long long {
#ifdef _WIN32
    return static_cast<unsigned long long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long long>(::getpid());
#endif
}

void cleanup_staged_path(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

}  // namespace

auto temporary_path_for(const std::filesystem::path& path, std::string_view tag)
    -> std::filesystem::path {
    static std::atomic<unsigned long long> counter{0};

    const auto filename =
        path.filename().string().empty() ? std::string("ppargo")
                                         : path.filename().string();
    const auto clock_now = std::chrono::steady_clock::now().time_since_epoch();
    const auto tick = static_cast<unsigned long long>(clock_now.count());
    const auto sequence = counter.fetch_add(1, std::memory_order_relaxed);
    const auto label = tag.empty() ? std::string("tmp") : std::string(tag);

    return path.parent_path() /
           std::format(".{}.{}.{}.{}.{}", filename, label, process_id(), tick,
                       sequence);
}

auto atomic_replace_file(const std::filesystem::path& staged_path,
                         const std::filesystem::path& final_path)
    -> util::Status {
    std::error_code ec;
    if (final_path.has_parent_path()) {
        std::filesystem::create_directories(final_path.parent_path(), ec);
        if (ec) {
            cleanup_staged_path(staged_path);
            return io_error("create parent directory", final_path.parent_path(),
                            ec);
        }
    }

#ifdef _WIN32
    if (!MoveFileExW(staged_path.wstring().c_str(), final_path.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        cleanup_staged_path(staged_path);
        return std::unexpected(util::make_error(
            "Failed to atomically replace file: " + final_path.string()));
    }
#else
    std::filesystem::rename(staged_path, final_path, ec);
    if (ec) {
        cleanup_staged_path(staged_path);
        return io_error("atomically replace file", final_path, ec);
    }
#endif

    return util::Ok;
}

auto publish_staged_file(const std::filesystem::path& staged_path,
                         const std::filesystem::path& final_path)
    -> util::Status {
    std::error_code ec;
    if (final_path.has_parent_path()) {
        std::filesystem::create_directories(final_path.parent_path(), ec);
        if (ec) {
            cleanup_staged_path(staged_path);
            return io_error("create parent directory", final_path.parent_path(),
                            ec);
        }
    }

    if (std::filesystem::exists(final_path, ec) && !ec) {
        cleanup_staged_path(staged_path);
        return util::Ok;
    }
    if (ec) {
        cleanup_staged_path(staged_path);
        return io_error("inspect staged file destination", final_path, ec);
    }

#ifdef _WIN32
    if (!MoveFileExW(staged_path.wstring().c_str(), final_path.wstring().c_str(),
                     MOVEFILE_WRITE_THROUGH)) {
        std::error_code exists_ec;
        if (std::filesystem::exists(final_path, exists_ec) && !exists_ec) {
            cleanup_staged_path(staged_path);
            return util::Ok;
        }
        cleanup_staged_path(staged_path);
        return std::unexpected(util::make_error(
            "Failed to publish staged file: " + final_path.string()));
    }
#else
    std::filesystem::create_hard_link(staged_path, final_path, ec);
    if (ec) {
        std::error_code exists_ec;
        if (std::filesystem::exists(final_path, exists_ec) && !exists_ec) {
            cleanup_staged_path(staged_path);
            return util::Ok;
        }
        cleanup_staged_path(staged_path);
        return io_error("publish staged file", final_path, ec);
    }
    cleanup_staged_path(staged_path);
#endif

    return util::Ok;
}

auto publish_staged_directory(const std::filesystem::path& staged_path,
                              const std::filesystem::path& final_path)
    -> util::Status {
    std::error_code ec;
    if (final_path.has_parent_path()) {
        std::filesystem::create_directories(final_path.parent_path(), ec);
        if (ec) {
            cleanup_staged_path(staged_path);
            return io_error("create parent directory", final_path.parent_path(),
                            ec);
        }
    }

    if (std::filesystem::exists(final_path, ec) && !ec) {
        cleanup_staged_path(staged_path);
        return util::Ok;
    }
    if (ec) {
        cleanup_staged_path(staged_path);
        return io_error("inspect staged directory destination", final_path, ec);
    }

    std::filesystem::rename(staged_path, final_path, ec);
    if (!ec) {
        return util::Ok;
    }

    std::error_code exists_ec;
    if (std::filesystem::exists(final_path, exists_ec) && !exists_ec) {
        cleanup_staged_path(staged_path);
        return util::Ok;
    }

    cleanup_staged_path(staged_path);
    return io_error("publish staged directory", final_path, ec);
}

auto atomic_write_text(const std::filesystem::path& path,
                       std::string_view content) -> util::Status {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return io_error("create parent directory", path.parent_path(), ec);
        }
    }

    const auto tmp = temporary_path_for(path, "tmp");

    {
        std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
        if (!out.is_open()) {
            return std::unexpected(
                util::make_error("Failed to open temp file: " + tmp.string()));
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out.good()) {
            cleanup_staged_path(tmp);
            return std::unexpected(
                util::make_error("Failed to write temp file: " + tmp.string()));
        }
    }

    return atomic_replace_file(tmp, path);
}

auto atomic_write_text_result(const std::filesystem::path& path,
                              std::string_view content) -> util::Status {
    return atomic_write_text(path, content);
}

}  // namespace util::fs
