#include "core/paths.hpp"

namespace core {

auto effective_output_dir(
    const Manifest& manifest,
    const std::optional<std::filesystem::path>& output_dir_override)
    -> std::filesystem::path {
    if (output_dir_override.has_value()) {
        return *output_dir_override;
    }
    return manifest.build.output_dir;
}

auto effective_module_output_dir(
    const Manifest& manifest,
    const std::optional<std::filesystem::path>& output_dir_override)
    -> std::filesystem::path {
    if (output_dir_override.has_value()) {
        return *output_dir_override / "modules";
    }
    return manifest.build.module_output_dir;
}

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
    return build_dir(root, manifest, release, std::nullopt);
}

auto build_dir(const std::filesystem::path& root, const Manifest& manifest,
               bool release,
               const std::optional<std::filesystem::path>& output_dir_override)
    -> std::filesystem::path {
    return root / effective_output_dir(manifest, output_dir_override) /
           (release ? "release" : "debug");
}

auto runner_cache_dir(const std::filesystem::path& root,
                      const Manifest& manifest) -> std::filesystem::path {
    return root / manifest.build.output_dir / "runners";
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
