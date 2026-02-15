#include "core/scaffold.hpp"

#include <filesystem>
#include <format>
#include <regex>

#include "core/manifest.hpp"
#include "core/templates.hpp"
#include "util/fs.hpp"


namespace {

auto write_file_if_missing(const std::filesystem::path& path, const std::string& content)
    -> util::Status {
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) {
        return util::Ok;
    }
    if (ec) {
        return std::unexpected(
            std::format("I/O Error: Failed to access path: {} ({})", path.string(),
                        ec.message()));
    }

    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return std::unexpected(std::format(
                "I/O Error: Failed to create directory: {} ({})", path.parent_path().string(),
                ec.message()));
        }
    }

    return util::fs::atomic_write_text_result(path, content);
}

auto has_path_traversal(const std::string& name) {
    return name.find("..") != std::string::npos;
}

}  // namespace

namespace core {

auto validate_project_name(const std::string& name) -> util::Status {
    if (name.empty()) {
        return std::unexpected("Validation Error: Project name cannot be empty.");
    }
    if (name == "." || name == ".." || has_path_traversal(name)) {
        return std::unexpected(
            "Validation Error: Project name must not include path traversal.");
    }
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        return std::unexpected(
            "Validation Error: Project name must not include path separators.");
    }

    static const std::regex allowed("^[A-Za-z0-9][A-Za-z0-9_-]*$");
    if (!std::regex_match(name, allowed)) {
        return std::unexpected(
            "Validation Error: Invalid project name. Allowed characters: letters, digits, '_', '-'.");
    }
    return util::Ok;
}

auto create_project_structure(const std::filesystem::path& root,
                              const std::string& name) -> util::Status {
    std::error_code ec;
    std::filesystem::create_directories(root / "src", ec);
    if (ec) {
        return std::unexpected(std::format(
            "I/O Error: Failed to create directory: {} ({})", (root / "src").string(),
            ec.message()));
    }

    const auto manifest_path = root / "ppargo.toml";
    if (std::error_code exists_ec; !std::filesystem::exists(manifest_path, exists_ec) && !exists_ec) {
        auto manifest = core::default_manifest(name);
        auto save = core::save_manifest(manifest_path, manifest);
        if (!save) {
            return std::unexpected(save.error());
        }
    } else if (exists_ec) {
        return std::unexpected(std::format(
            "I/O Error: Failed to access manifest path: {} ({})",
            manifest_path.string(), exists_ec.message()));
    }

    auto main_write = write_file_if_missing(root / "src" / "main.cpp",
                                            core::templates::main_cpp_template());
    if (!main_write) {
        return std::unexpected(main_write.error());
    }
    auto gitignore_write = write_file_if_missing(root / ".gitignore",
                                                 core::templates::gitignore_template());
    if (!gitignore_write) {
        return std::unexpected(gitignore_write.error());
    }

    return util::Ok;
}

}  // namespace core



