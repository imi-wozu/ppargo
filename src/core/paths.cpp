#include "core/paths.hpp"


namespace core {

auto find_project_root(const std::filesystem::path& start)
    -> util::Result<std::filesystem::path> {
    std::error_code ec;
    auto current = start.empty() ? std::filesystem::current_path(ec)
                                 : std::filesystem::absolute(start, ec);
    if (ec) {
        return std::unexpected(
            "I/O Error: Failed to resolve current working directory.");
    }

    while (true) {
        if (std::filesystem::exists(current / "ppargo.toml", ec)) {
            return current;
        }
        if (ec) {
            return std::unexpected(
                "I/O Error: Failed while searching for `ppargo.toml`.");
        }

        if (!current.has_parent_path() || current.parent_path() == current) {
            return std::unexpected(
                "Could not find `ppargo.toml` in current directory or any parent.");
        }
        current = current.parent_path();
    }
}

auto build_dir(const std::filesystem::path& root, const Manifest& manifest,
               bool release) -> std::filesystem::path {
    return root / manifest.build.output_dir / (release ? "release" : "debug");
}

auto binary_name(const Manifest& manifest) -> std::string {
    std::string name = manifest.build.binary_name.empty() ? manifest.package.name
                                                           : manifest.build.binary_name;

#ifdef _WIN32
    if (name.size() < 4 || name.substr(name.size() - 4) != ".exe") {
        name += ".exe";
    }
#endif
    return name;
}

auto detect_triplet() -> std::string {
#ifdef _WIN32
#if defined(_M_ARM64) || defined(__aarch64__)
    return "arm64-windows";
#else
    return "x64-windows";
#endif
#elif defined(__APPLE__)
#if defined(__aarch64__)
    return "arm64-osx";
#else
    return "x64-osx";
#endif
#else
#if defined(__aarch64__)
    return "arm64-linux";
#else
    return "x64-linux";
#endif
#endif
}

}  // namespace core



