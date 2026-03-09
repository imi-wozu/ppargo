#include "core/paths.hpp"

namespace core {

auto find_project_root(const std::filesystem::path& start) noexcept
    -> util::Result<std::filesystem::path> {
    std::error_code ec;
    auto current = start.empty() ? std::filesystem::current_path(ec)
                                 : std::filesystem::absolute(start, ec);
    if (ec) {
        return util::make_unexpected(
            "Failed to resolve current working directory: " + ec.message());
    }

    while (true) {
        if (std::filesystem::exists(current / "ppargo.toml", ec)) {
            return current;
        }
        if (ec) {
            return util::make_unexpected(
                "Failed while searching for `ppargo.toml`: " + ec.message());
        }

        if (!current.has_parent_path() || current.parent_path() == current) {
            return util::make_unexpected(
                "Could not find `ppargo.toml` in current directory or any "
                "parent.");
        }
        current = current.parent_path();
    }
}

auto build_dir(const std::filesystem::path& root, const Manifest& manifest,
               bool release) -> std::filesystem::path {
    return root / manifest.build.output_dir / (release ? "release" : "debug");
}

auto binary_name(const Manifest& manifest) -> std::string {
    std::string name = manifest.build.binary_name.empty()
                           ? manifest.package.name
                           : manifest.build.binary_name;

#ifdef _WIN32
    if (!name.ends_with(".exe")) {
        name += ".exe";
    }
#endif
    return name;
}
}  // namespace core
