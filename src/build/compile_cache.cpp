#include "build/compile_cache.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "util/fs.hpp"

namespace build::compile::detail {

auto recreate_directory(const std::filesystem::path& dir) -> util::Status {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (ec) {
        return std::unexpected(util::make_error("Failed to remove directory: " +
                                   dir.string() + " (" + ec.message() + ")"));
    }

    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return std::unexpected(util::make_error("Failed to create directory: " +
                                   dir.string() + " (" + ec.message() + ")"));
    }
    return util::Ok;
}

auto file_stamp(const std::filesystem::path& path) -> util::Result<long long> {
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::unexpected(util::make_error("Failed to read timestamp: " +
                                   path.string() + " (" + ec.message() + ")"));
    }
    return static_cast<long long>(stamp.time_since_epoch().count());
}

auto file_size_value(const std::filesystem::path& path)
    -> util::Result<std::uintmax_t> {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return std::unexpected(util::make_error("Failed to read file size: " +
                                   path.string() + " (" + ec.message() + ")"));
    }
    return size;
}

auto signature_hash_text(std::string_view  signature) -> std::string {
    return std::to_string(std::hash<std::string_view>{}(signature));
}

auto load_check_cache(const std::filesystem::path& cache_file,
                      std::string_view  signature_hash)
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
    if (!std::getline(input, line) || line != (std::string{"sig="} + std::string(signature_hash))) {
        return cache;
    }

    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        const auto first_tab = line.find('\t');
        const auto second_tab =
            line.find('\t', first_tab == std::string::npos ? 0 : first_tab + 1);
        const auto third_tab =
            line.find('\t', second_tab == std::string::npos ? 0 : second_tab + 1);
        if (first_tab == std::string::npos || second_tab == std::string::npos) {
            continue;
        }

        const std::string path = line.substr(0, first_tab);
        const std::string mtime_text =
            line.substr(first_tab + 1, second_tab - first_tab - 1);
        const std::string size_text =
            third_tab == std::string::npos
                ? line.substr(second_tab + 1)
                : line.substr(second_tab + 1, third_tab - second_tab - 1);
        const std::string dependency_recency_text =
            third_tab == std::string::npos ? std::string{} : line.substr(third_tab + 1);

        auto mtime_stream = std::istringstream(mtime_text);
        auto size_stream = std::istringstream(size_text);
        auto dependency_recency_stream =
            std::istringstream(dependency_recency_text);
        long long mtime = 0;
        std::uintmax_t size = 0;
        long long dependency_recency = 0;
        if ((mtime_stream >> mtime) && (size_stream >> size)) {
            if (dependency_recency_text.empty() ||
                !(dependency_recency_stream >> dependency_recency)) {
                dependency_recency = mtime;
            }
            cache[path] = CheckCacheEntry{mtime, size, dependency_recency};
        }
    }

    return cache;
}

auto write_check_cache(
    const std::filesystem::path& cache_file, std::string_view  signature_hash,
    const std::unordered_map<std::string, CheckCacheEntry>& cache) -> util::Status {
    std::ostringstream out;
    out << "sig=" << signature_hash << "\n";
    for (const auto& [path, entry] : cache) {
        out << path << "\t" << entry.mtime << "\t" << entry.size << "\t"
            << entry.dependency_recency << "\n";
    }
    return util::fs::atomic_write_text_result(cache_file, out.str());
}

}  // namespace build::compile::detail

namespace build::compile {}




