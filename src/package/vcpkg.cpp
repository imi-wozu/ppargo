#include "package/vcpkg.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <regex>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "core/paths.hpp"
#include "util/fs.hpp"
#include "util/process.hpp"
#include "util/result.hpp"
#include "util/text.hpp"

namespace {

auto escape_json_string(std::string_view input) {
    std::string escaped;
    escaped.reserve(input.size() + 2);
    for (char ch : input) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

auto package_install_root(const std::filesystem::path& project_root)
    -> std::filesystem::path {
    return project_root / "packages";
}

auto vcpkg_info_root(const std::filesystem::path& project_root)
    -> std::filesystem::path {
    return package_install_root(project_root) / "vcpkg" / "info";
}

auto normalize_package_name(std::string_view value) -> std::string {
    const auto colon = value.find(':');
    return std::string(value.substr(0, colon));
}

auto path_exists(const std::filesystem::path& path) -> util::Result<bool> {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        return std::unexpected(util::make_error(std::format(
            "Failed to access {} ({})", path.string(), ec.message())));
    }
    return exists;
}

auto installed_list_path(const std::filesystem::path& project_root,
                         const package::ResolvedPackage& package,
                         std::string_view triplet) -> std::filesystem::path {
    return vcpkg_info_root(project_root) /
           std::format("{}_{}_{}.list", normalize_package_name(package.name),
                       package.version, triplet);
}

auto package_list_has_installed_entries(
    const std::filesystem::path& install_root,
    const std::filesystem::path& list_file) -> util::Result<bool> {
    std::ifstream input(list_file);
    if (!input.is_open()) {
        return std::unexpected(util::make_error(std::format(
            "Failed to open installed package list: {}", list_file.string())));
    }

    std::string line;
    while (std::getline(input, line)) {
        const auto entry = util::text::trim_ascii_copy(line);
        if (entry.empty()) {
            continue;
        }

        const auto installed_path = install_root / std::filesystem::path(entry);
        const auto exists = GUARD(path_exists(installed_path));
        if (exists) {
            return true;
        }
    }

    return false;
}

auto dependencies_array_bounds(std::string_view json)
    -> util::Result<std::pair<std::size_t, std::size_t>> {
    const auto key = json.find("\"dependencies\"");
    if (key == std::string::npos) {
        return std::unexpected(util::make_error(
            "Invalid vcpkg.json: missing 'dependencies' field."));
    }

    const auto colon = json.find(':', key);
    if (colon == std::string::npos) {
        return std::unexpected(util::make_error(
            "Invalid vcpkg.json: malformed 'dependencies' declaration."));
    }

    const auto open = json.find('[', colon);
    if (open == std::string::npos) {
        return std::unexpected(util::make_error(
            "Invalid vcpkg.json: 'dependencies' is not an array."));
    }

    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    for (std::size_t i = open; i < json.size(); ++i) {
        const char ch = json[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '[') {
            ++depth;
            continue;
        }
        if (ch == ']') {
            --depth;
            if (depth == 0) {
                return std::pair<std::size_t, std::size_t>{open, i};
            }
        }
    }

    return std::unexpected(util::make_error(
        "Invalid vcpkg.json: unterminated dependencies array."));
}

auto collect_dependency_names(const package::ResolvedGraph& graph)
    -> std::vector<std::string> {
    std::vector<std::string> names;
    names.reserve(graph.packages.size());

    std::unordered_set<std::string> seen;
    seen.reserve(graph.packages.size());

    for (const auto& package : graph.packages) {
        if (!seen.insert(package.name).second) {
            continue;
        }
        names.push_back(package.name);
    }

    return names;
}

auto render_dependencies_array(std::span<const std::string> package_names)
    -> std::string {
    if (package_names.empty()) {
        return "[]";
    }

    std::ostringstream out;
    out << "[\n";
    for (std::size_t i = 0; i < package_names.size(); ++i) {
        out << "    \"" << escape_json_string(package_names[i]) << "\"";
        if (i + 1 < package_names.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]";
    return out.str();
}

}  // namespace

namespace package::vcpkg {

auto vcpkg_executable(const core::Manifest& manifest)
    -> util::Result<std::filesystem::path> {
    if (manifest.features.vcpkg_root.empty()) {
        return std::unexpected(
            util::make_error("vcpkg_root must be set in [features] when "
                             "package_manager is vcpkg."));
    }

#ifdef _WIN32
    const auto exe = manifest.features.vcpkg_root / "vcpkg.exe";
#else
    const auto exe = manifest.features.vcpkg_root / "vcpkg";
#endif

    std::error_code ec;
    if (!std::filesystem::exists(exe, ec) || ec) {
        return std::unexpected(
            util::make_error("vcpkg executable not found: " + exe.string()));
    }
    return exe;
}

auto ensure_vcpkg_manifest(const std::filesystem::path& project_root)
    -> util::Status {
    const auto file = project_root / "vcpkg.json";
    std::error_code ec;
    if (std::filesystem::exists(file, ec) && !ec) {
        return util::Ok;
    }
    if (ec) {
        return std::unexpected(util::make_error(
            "Failed to access vcpkg manifest path: " + file.string()));
    }

    std::ostringstream content;
    content << "{\n";
    content
        << "  \"$schema\": "
           "\"https://raw.githubusercontent.com/microsoft/vcpkg/master/scripts/"
           "vcpkg.schema.json\",\n";
    content << "  \"dependencies\": []\n";
    content << "}\n";

    return util::fs::atomic_write_text_result(file, content.str());
}

auto parse_package_list(std::string_view output)
    -> util::Result<std::vector<PackageInfo>> {
    std::vector<PackageInfo> packages;
    std::istringstream stream{std::string(output)};
    std::string line;

    while (std::getline(stream, line)) {
        line = util::text::trim_ascii_copy(line);
        if (line.empty()) {
            continue;
        }

        std::istringstream row(line);
        std::string name;
        std::string version;
        if (!(row >> name >> version)) {
            continue;
        }
        if (version.empty() ||
            !std::isdigit(static_cast<unsigned char>(version.front()))) {
            continue;
        }

        std::string description;
        std::getline(row, description);
        description = util::text::trim_ascii_copy(description);
        packages.push_back(PackageInfo{name, version, description});
    }

    return packages;
}

auto search_packages(const core::Manifest& manifest, std::string_view package)
    -> util::Result<std::vector<PackageInfo>> {
    auto exe = vcpkg_executable(manifest);
    if (!exe) {
        return std::unexpected(exe.error());
    }

    util::process::RunOptions options;
    options.capture_output = true;
    options.merge_stderr = true;

    const std::vector<std::string> args{"search", std::string(package),
                                        "--x-full-desc"};
    auto result = util::process::run_result(*exe, args, options);
    if (!result) {
        return std::unexpected(result.error());
    }

    if (result->exit_code != 0) {
        const auto details = util::text::trim_ascii_copy(result->output);
        return std::unexpected(util::make_error(
            "Failed to search for package '" + std::string(package) +
            "' via vcpkg.\n" + details));
    }

    return parse_package_list(result->output);
}

auto sync_dependencies(const std::filesystem::path& project_root,
                       const package::ResolvedGraph& graph) -> util::Status {
    const auto file = project_root / "vcpkg.json";
    auto ensure = ensure_vcpkg_manifest(project_root);
    if (!ensure) {
        return std::unexpected(ensure.error());
    }

    std::ifstream input(file);
    if (!input.is_open()) {
        return std::unexpected(util::make_error(
            "Failed to open vcpkg manifest: " + file.string()));
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    std::string json = buffer.str();
    input.close();

    auto bounds = GUARD(dependencies_array_bounds(json));
    const auto [open, close] = bounds;
    const auto package_names = collect_dependency_names(graph);
    json.replace(open, close - open + 1,
                 render_dependencies_array(package_names));

    return util::fs::atomic_write_text_result(file, json);
}

auto required_packages_installed(const std::filesystem::path& project_root,
                                 const package::ResolvedGraph& graph)
    -> util::Result<bool> {
    if (graph.packages.empty()) {
        return true;
    }

    const auto install_root = package_install_root(project_root);
    const auto triplet = core::detect_triplet();
    const auto triplet_root = install_root / triplet;
    const auto info_root = vcpkg_info_root(project_root);

    const auto triplet_exists = GUARD(path_exists(triplet_root));
    if (!triplet_exists) {
        return false;
    }

    const auto info_exists = GUARD(path_exists(info_root));
    if (!info_exists) {
        return false;
    }

    for (const auto& package : graph.packages) {
        const auto list_file =
            installed_list_path(project_root, package, triplet);
        const auto list_exists = GUARD(path_exists(list_file));
        if (!list_exists) {
            return false;
        }

        const auto has_entries =
            GUARD(package_list_has_installed_entries(install_root, list_file));
        if (!has_entries) {
            return false;
        }
    }

    return true;
}

auto install_dependencies(const std::filesystem::path& project_root,
                          const core::Manifest& manifest,
                          const package::ResolvedGraph& graph) -> util::Status {
    auto ensure = ensure_vcpkg_manifest(project_root);
    if (!ensure) {
        return std::unexpected(ensure.error());
    }

    const auto installed =
        GUARD(required_packages_installed(project_root, graph));
    if (installed) {
        return util::Ok;
    }

    auto exe = vcpkg_executable(manifest);
    if (!exe) {
        return std::unexpected(exe.error());
    }

    const std::vector<std::string> args = {
        std::string{"install"},
        std::string{"--x-manifest-root"},
        project_root.string(),
        std::string{"--x-install-root"},
        package_install_root(project_root).string(),
        std::string{"--triplet"},
        std::string{core::detect_triplet()},
    };

    util::process::RunOptions options{};
    options.capture_output = true;
    options.merge_stderr = true;
    auto result = util::process::run_result(*exe, args, options);
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->exit_code != 0) {
        const auto details = util::text::trim_ascii_copy(result->output);
        if (details.empty()) {
            return std::unexpected(
                util::make_error("Failed to install dependencies via vcpkg."));
        }
        return std::unexpected(util::make_error(
            "Failed to install dependencies via vcpkg.\n" + details));
    }

    const auto verified =
        GUARD(required_packages_installed(project_root, graph));
    if (!verified) {
        return std::unexpected(
            util::make_error("vcpkg install completed, but required packages "
                             "are still missing from the local install root."));
    }
    return util::Ok;
}

}  // namespace package::vcpkg
