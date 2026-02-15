#include "util/fs.hpp"

#include <cstdlib>
#include <fstream>
#include <format>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif


namespace util::fs {

auto atomic_write_text(const std::filesystem::path& path,
                       std::string_view content) -> util::Status {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return std::unexpected(std::format(
                "I/O Error: Failed to create parent directory: {} ({})",
                path.parent_path().string(), ec.message()));
        }
    }

    const auto tmp = path.parent_path() /
                     (path.filename().string() + ".tmp." +
                      std::to_string(std::rand()));

    {
        std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
        if (!out.is_open()) {
            return std::unexpected("I/O Error: Failed to open temp file: " +
                                   tmp.string());
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out.good()) {
            return std::unexpected("I/O Error: Failed to write temp file: " +
                                   tmp.string());
        }
    }

#ifdef _WIN32
    if (!MoveFileExW(tmp.wstring().c_str(), path.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(tmp, ec);
        return std::unexpected("I/O Error: Failed to atomically replace file: " +
                               path.string());
    }
#else
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return std::unexpected("I/O Error: Failed to atomically replace file: " +
                               path.string());
    }
#endif

    return util::Ok;
}

auto atomic_write_text_result(const std::filesystem::path& path,
                              std::string_view content) -> util::Status {
    return atomic_write_text(path, content);
}

}  // namespace util::fs




