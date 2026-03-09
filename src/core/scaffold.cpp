#include "core/scaffold.hpp"

#include <string_view>

#include "core/manifest.hpp"
#include "core/templates.hpp"
#include "util/fs.hpp"

namespace {

auto write_file_if_missing(const std::filesystem::path& path,
                           std::string_view content) -> util::Status {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return util::Ok;
    }
    if (ec) {
        return util::make_unexpected(std::format(
            "Failed to access path: {} ({})", path.string(), ec.message()));
    }

    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return util::make_unexpected(
                std::format("Failed to create directory: {} ({})",
                            path.parent_path().string(), ec.message()));
        }
    }

    return util::fs::atomic_write_text_result(path, content);
}

constexpr bool has_path_traversal(std::string_view name) {
    return name.contains("..");
}

}  // namespace

namespace core {

auto validate_project_name(std::string_view name) -> util::Status {
    if (name.empty()) {
        return util::make_unexpected("Project name cannot be empty.");
    }
    if (name == "." || name == ".." || has_path_traversal(name)) {
        return util::make_unexpected(
            "Project name must not include path traversal.");
    }
    if (name.contains('/') || name.contains('\\')) {
        return util::make_unexpected(
            "Project name must not include path separators.");
    }

    if (!std::isalnum(name.front())) {
        return util::make_unexpected(
            "Invalid project name. Allowed characters: "
            "letters, digits.");
    }
    for (char c : name) {
        if (!std::isalnum(c) && c != '_' && c != '-') {
            return util::make_unexpected(
                "Invalid project name. Allowed characters: "
                "letters, digits, '_', '-'.");
        }
    }

    return util::Ok;
}

auto create_project_structure(const std::filesystem::path& root,
                              std::string_view name) -> util::Status {
    std::error_code ec;
    std::filesystem::create_directories(root / "src", ec);
    if (ec) {
        return util::make_unexpected(
            std::format("Failed to create directory: {} ({})",
                        (root / "src").string(), ec.message()));
    }

    const auto manifest_path = root / "ppargo.toml";
    if (std::error_code exists_ec;
        !std::filesystem::exists(manifest_path, exists_ec) && !exists_ec) {
        auto save =
            core::save_manifest(manifest_path, core::default_manifest(name));
        if (!save) {
            return std::unexpected(save.error());
        }
    } else if (exists_ec) {
        return util::make_unexpected(
            std::format("Failed to access manifest path: {} ({})",
                        manifest_path.string(), exists_ec.message()));
    }

    auto main_write = write_file_if_missing(
        root / "src" / "main.cpp", core::templates::main_cpp_template());
    if (!main_write) {
        return std::unexpected(main_write.error());
    }
    auto gitignore_write = write_file_if_missing(
        root / ".gitignore", core::templates::gitignore_template());
    if (!gitignore_write) {
        return std::unexpected(gitignore_write.error());
    }

    return util::Ok;
}

}  // namespace core
