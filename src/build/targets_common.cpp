#include "build/targets_internal.hpp"

#include <span>

namespace build::targets::detail {

auto is_cpp_source_extension(std::string_view ext) -> bool {
    return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c";
}

auto is_cpp_source_path(const std::filesystem::path& path) -> bool {
    return is_cpp_source_extension(path.extension().string());
}

auto is_regular_file_path(const std::filesystem::path& path) -> bool {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

auto is_directory_path(const std::filesystem::path& path) -> bool {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec) && !ec;
}

auto canonical_existing_path(const std::filesystem::path& path)
    -> util::Result<std::filesystem::path> {
    std::error_code ec;
    const auto canonical{std::filesystem::weakly_canonical(path, ec)};
    if (ec) {
        return std::unexpected(util::make_error(std::format(
            "Failed to canonicalize path: {} ({})", path.string(),
            ec.message())));
    }
    return canonical;
}

auto collect_recursive_cpp_sources(const std::filesystem::path& root)
    -> util::Result<std::vector<std::filesystem::path>> {
    std::vector<std::filesystem::path> files{};

    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec) {
        if (ec) {
            return std::unexpected(util::make_error(std::format(
                "Failed to access path: {} ({})", root.string(),
                ec.message())));
        }
        return files;
    }

    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end;
         it.increment(ec)) {
        if (ec) {
            return std::unexpected(util::make_error(std::format(
                "Failed while scanning path: {} ({})", root.string(),
                ec.message())));
        }
        if (!it->is_regular_file(ec)) {
            if (ec) {
                return std::unexpected(util::make_error(std::format(
                    "Failed to inspect path entry: {} ({})",
                    it->path().string(), ec.message())));
            }
            continue;
        }
        if (!is_cpp_source_extension(it->path().extension().string())) {
            continue;
        }
        auto canonical_source = GUARD(canonical_existing_path(it->path()));
        files.push_back(std::move(canonical_source));
    }

    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

void sort_targets(std::vector<Target>& targets) {
    std::sort(targets.begin(), targets.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
}

auto format_named_choices(std::span<const Target>  targets) -> std::string {
    std::string out{};
    for (const auto& target : targets) {
        if (!out.empty()) {
            out += ", ";
        }
        out += target.name;
    }
    return out;
}

}  // namespace build::targets::detail


