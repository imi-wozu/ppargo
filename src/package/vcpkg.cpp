#include "package/vcpkg.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "core/paths.hpp"
#include "util/fs.hpp"
#include "util/process.hpp"


namespace {

auto trim(const std::string& value) {
    const std::string whitespace = " \t\r\n";
    const auto begin = value.find_first_not_of(whitespace);
    if (begin == std::string::npos) {
        return std::string{};
    }
    const auto end = value.find_last_not_of(whitespace);
    return value.substr(begin, end - begin + 1);
}

auto escape_json_string(const std::string& input) {
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

auto dependencies_array_bounds(const std::string& json)
    -> util::Result<std::pair<std::size_t, std::size_t>> {
    const auto key = json.find("\"dependencies\"");
    if (key == std::string::npos) {
        return std::unexpected(
            "Manifest Error: Invalid vcpkg.json: missing 'dependencies' field.");
    }

    const auto colon = json.find(':', key);
    if (colon == std::string::npos) {
        return std::unexpected(
            "Manifest Error: Invalid vcpkg.json: malformed 'dependencies' declaration.");
    }

    const auto open = json.find('[', colon);
    if (open == std::string::npos) {
        return std::unexpected(
            "Manifest Error: Invalid vcpkg.json: 'dependencies' is not an array.");
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

    return std::unexpected(
        "Manifest Error: Invalid vcpkg.json: unterminated dependencies array.");
}

auto read_string_literals(const std::string& input) {
    std::unordered_set<std::string> values;
    bool in_string = false;
    bool escaped = false;
    std::string current;

    for (char ch : input) {
        if (!in_string) {
            if (ch == '"') {
                in_string = true;
                current.clear();
            }
            continue;
        }

        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }

        if (ch == '"') {
            values.insert(current);
            in_string = false;
            continue;
        }

        current.push_back(ch);
    }

    return values;
}

}  // namespace

namespace package::vcpkg {

auto vcpkg_executable(const core::Manifest& manifest)
    -> util::Result<std::filesystem::path> {
    if (!manifest.features.has_vcpkg_root) {
        return std::unexpected(
            "Manifest Error: vcpkg_root must be set in [features] when package_manager is vcpkg.");
    }

#ifdef _WIN32
    const auto exe = manifest.features.vcpkg_root / "vcpkg.exe";
#else
    const auto exe = manifest.features.vcpkg_root / "vcpkg";
#endif

    std::error_code ec;
    if (!std::filesystem::exists(exe, ec) || ec) {
        return std::unexpected("Process Error: vcpkg executable not found: " +
                               exe.string());
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
        return std::unexpected("I/O Error: Failed to access vcpkg manifest path: " +
                               file.string());
    }

    std::ostringstream content;
    content << "{\n";
    content << "  \"$schema\": "
               "\"https://raw.githubusercontent.com/microsoft/vcpkg/master/scripts/"
               "vcpkg.schema.json\",\n";
    content << "  \"dependencies\": []\n";
    content << "}\n";

    return util::fs::atomic_write_text_result(file, content.str());
}

auto parse_package_list(const std::string& output)
    -> util::Result<std::vector<PackageInfo>> {
    std::vector<PackageInfo> packages;
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        std::istringstream row(line);
        std::string name;
        std::string version;
        if (!(row >> name >> version)) {
            continue;
        }
        if (version.empty() || !std::isdigit(static_cast<unsigned char>(version.front()))) {
            continue;
        }

        std::string description;
        std::getline(row, description);
        description = trim(description);
        packages.push_back(PackageInfo{name, version, description});
    }

    return packages;
}

auto search_packages(const core::Manifest& manifest,
                     const std::string& package)
    -> util::Result<std::vector<PackageInfo>> {
    auto exe = vcpkg_executable(manifest);
    if (!exe) {
        return std::unexpected(exe.error());
    }

    util::process::RunOptions options;
    options.capture_output = true;
    options.merge_stderr = true;

    auto result = util::process::run_result(
        *exe, {"search", package, "--x-full-desc"}, options);
    if (!result) {
        return std::unexpected(result.error());
    }

    if (result->exit_code != 0) {
        const auto details = trim(result->output);
        return std::unexpected("Process Error: Failed to search for package '" + package +
                               "' via vcpkg.\n" + details);
    }

    return parse_package_list(result->output);
}

auto upsert_dependency(const std::filesystem::path& project_root,
                       const std::string& package_name) -> util::Status {
    const auto file = project_root / "vcpkg.json";
    auto ensure = ensure_vcpkg_manifest(project_root);
    if (!ensure) {
        return std::unexpected(ensure.error());
    }

    std::ifstream input(file);
    if (!input.is_open()) {
        return std::unexpected("I/O Error: Failed to open vcpkg manifest: " +
                               file.string());
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    std::string json = buffer.str();
    input.close();

    auto bounds = TRY(dependencies_array_bounds(json));
    const auto [open, close] = bounds;
    const auto inner = json.substr(open + 1, close - open - 1);
    const auto values = read_string_literals(inner);
    if (values.find(package_name) != values.end()) {
        return util::Ok;
    }

    const std::string escaped = escape_json_string(package_name);
    const std::string insertion =
        trim(inner).empty() ? ("\n    \"" + escaped + "\"\n  ")
                            : (",\n    \"" + escaped + "\"");
    json.insert(close, insertion);

    return util::fs::atomic_write_text_result(file, json);
}

auto install_dependencies(const std::filesystem::path& project_root,
                          const core::Manifest& manifest) -> util::Status {
    auto ensure = ensure_vcpkg_manifest(project_root);
    if (!ensure) {
        return std::unexpected(ensure.error());
    }
    auto exe = vcpkg_executable(manifest);
    if (!exe) {
        return std::unexpected(exe.error());
    }

    const std::vector<std::string> args = {
        "install", "--x-manifest-root", project_root.string(),
        "--x-install-root", (project_root / "packages").string(),
        "--triplet", core::detect_triplet()};

    auto result = util::process::run_result(*exe, args);
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->exit_code != 0) {
        return std::unexpected(
            "Process Error: Failed to install dependencies via vcpkg.");
    }
    return util::Ok;
}

}  // namespace package::vcpkg



