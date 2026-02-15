#include "core/manifest.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "util/fs.hpp"


namespace {

enum class Section {
    None,
    Package,
    Ppargo,
    Toolchain,
    LegacyFeatures,
    Build,
    Dependencies,
};

auto trim(const std::string& value) {
    const std::string whitespace = " \t\r\n";
    const auto begin = value.find_first_not_of(whitespace);
    if (begin == std::string::npos) {
        return std::string{};
    }
    const auto end = value.find_last_not_of(whitespace);
    return value.substr(begin, end - begin + 1);
}

auto strip_comment(const std::string& line) {
    bool in_single = false;
    bool in_double = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\'' && !in_double) {
            in_single = !in_single;
        } else if (line[i] == '"' && !in_single) {
            in_double = !in_double;
        } else if (line[i] == '#' && !in_single && !in_double) {
            return line.substr(0, i);
        }
    }
    return line;
}

auto unquote(const std::string& input) {
    const auto value = trim(input);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        const auto inner = value.substr(1, value.size() - 2);
        std::string unescaped;
        unescaped.reserve(inner.size());
        bool escaped = false;
        for (char ch : inner) {
            if (!escaped) {
                if (ch == '\\') {
                    escaped = true;
                } else {
                    unescaped.push_back(ch);
                }
                continue;
            }

            switch (ch) {
                case '\\':
                    unescaped.push_back('\\');
                    break;
                case '"':
                    unescaped.push_back('"');
                    break;
                case 'n':
                    unescaped.push_back('\n');
                    break;
                case 'r':
                    unescaped.push_back('\r');
                    break;
                case 't':
                    unescaped.push_back('\t');
                    break;
                default:
                    unescaped.push_back(ch);
                    break;
            }
            escaped = false;
        }
        if (escaped) {
            unescaped.push_back('\\');
        }
        return unescaped;
    }
    if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

auto is_quoted(const std::string& input) {
    const auto value = trim(input);
    if (value.size() < 2) {
        return false;
    }
    return (value.front() == '"' && value.back() == '"') ||
           (value.front() == '\'' && value.back() == '\'');
}

auto parse_bool(const std::string& input) -> util::Result<bool> {
    const auto value = trim(input);
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    return std::unexpected("Invalid boolean value: " + value);
}

auto parse_string_array(const std::string& input)
    -> util::Result<std::vector<std::string>> {
    auto value = trim(input);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        return std::unexpected("Invalid TOML array value: " + value);
    }

    value = value.substr(1, value.size() - 2);
    std::vector<std::string> result;
    std::string current;
    bool in_single = false;
    bool in_double = false;
    bool escaped = false;

    for (char ch : value) {
        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_double) {
            current.push_back(ch);
            escaped = true;
            continue;
        }
        if (ch == '\'' && !in_double) {
            in_single = !in_single;
            current.push_back(ch);
            continue;
        }
        if (ch == '"' && !in_single) {
            in_double = !in_double;
            current.push_back(ch);
            continue;
        }
        if (ch == ',' && !in_single && !in_double) {
                const auto item = trim(current);
                if (!item.empty()) {
                    if (!is_quoted(item)) {
                        return std::unexpected(
                            "Invalid TOML array value (non-string element): " + input);
                    }
                    result.push_back(unquote(item));
                }
            current.clear();
            continue;
        }
        current.push_back(ch);
    }

    if (in_single || in_double || escaped) {
        return std::unexpected(
            "Invalid TOML array value (unterminated string): " + input);
    }

    const auto last_item = trim(current);
    if (!last_item.empty()) {
        if (!is_quoted(last_item)) {
            return std::unexpected(
                "Invalid TOML array value (non-string element): " + input);
        }
        result.push_back(unquote(last_item));
    }

    return result;
}

auto toml_quote(std::string value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return "\"" + escaped + "\"";
}

auto path_as_toml_string(const std::filesystem::path& path) {
    return path.generic_string();
}

auto array_to_toml(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << toml_quote(values[i]);
    }
    out << "]";
    return out.str();
}

}  // namespace

namespace core {

auto default_manifest(const std::string& package_name) -> Manifest {
    Manifest manifest;
    manifest.package.name = package_name;
    manifest.package.version = "0.1.0";
    manifest.package.edition = "cpp23";

    manifest.toolchain.compiler = "clang++";
    manifest.toolchain.linker = "lld";

    manifest.features.packages = true;
    manifest.features.package_manager = "vcpkg";
    manifest.features.has_vcpkg_root = false;

    manifest.build.source_dir = "src";
    manifest.build.include_dirs = {"src", "include"};
    manifest.build.exclude = {"target/**"};
    manifest.build.output_dir = "target/cpp";
    manifest.build.binary_name.clear();

    std::string env_vcpkg_root;
#ifdef _WIN32
    char* raw = nullptr;
    std::size_t raw_len = 0;
    if (_dupenv_s(&raw, &raw_len, "VCPKG_ROOT") == 0 && raw != nullptr && raw[0] != '\0') {
        env_vcpkg_root = raw;
    }
    std::free(raw);
#else
    const char* raw = std::getenv("VCPKG_ROOT");
    if (raw != nullptr && raw[0] != '\0') {
        env_vcpkg_root = raw;
    }
#endif

    if (!env_vcpkg_root.empty()) {
        manifest.features.vcpkg_root = std::filesystem::path(env_vcpkg_root);
    } else {
#ifdef _WIN32
        manifest.features.vcpkg_root = std::filesystem::path("C:/vcpkg");
#else
        manifest.features.vcpkg_root = std::filesystem::path("/usr/local/vcpkg");
#endif
    }
    manifest.features.has_vcpkg_root = true;

    return manifest;
}

auto load_manifest(const std::filesystem::path& path) -> util::Result<Manifest> {
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::unexpected("Failed to open manifest: " + path.string());
    }

    Manifest manifest =
        default_manifest(path.parent_path().filename().string().empty()
                             ? "ppargo"
                             : path.parent_path().filename().string());

    Section section = Section::None;
    std::unordered_map<int, std::unordered_set<std::string>> seen_keys;
    bool first_line = true;

    const auto ensure_unique =
        [&seen_keys](Section current_section, const std::string& key) -> util::Status {
        auto& keys = seen_keys[static_cast<int>(current_section)];
        if (!keys.insert(key).second) {
            return std::unexpected("Manifest Error: Duplicate key '" + key + "'.");
        }
        return util::Ok;
    };

    std::string line;
    while (std::getline(input, line)) {
        if (first_line) {
            first_line = false;
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }
        }
        line = trim(strip_comment(line));
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            const auto header = trim(line.substr(1, line.size() - 2));
            if (header == "package") {
                section = Section::Package;
            } else if (header == "package.metadata.ppargo") {
                section = Section::Ppargo;
            } else if (header == "package.metadata.ppargo.toolchain") {
                section = Section::Toolchain;
            } else if (header == "package.metadata.ppargo.build") {
                section = Section::Build;
            } else if (header == "package.metadata.ppargo.features") {
                section = Section::LegacyFeatures;
            } else if (header == "toolchain") {
                section = Section::Toolchain;
            } else if (header == "features") {
                section = Section::LegacyFeatures;
            } else if (header == "build") {
                section = Section::Build;
            } else if (header == "dependencies") {
                section = Section::Dependencies;
            } else {
                section = Section::None;
            }
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        const auto key = trim(line.substr(0, eq));
        const auto value = trim(line.substr(eq + 1));

        switch (section) {
            case Section::Package: {
                if (key == "name") {
                    TRY_void(ensure_unique(section, key));
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            "Manifest Error: [package].name must be a string.");
                    }
                    manifest.package.name = unquote(value);
                } else if (key == "version") {
                    TRY_void(ensure_unique(section, key));
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            "Manifest Error: [package].version must be a string.");
                    }
                    manifest.package.version = unquote(value);
                } else if (key == "edition") {
                    TRY_void(ensure_unique(section, key));
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            "Manifest Error: [package].edition must be a string.");
                    }
                    manifest.package.edition = unquote(value);
                } else {
                    return std::unexpected("Manifest Error: Unknown key in [package]: " +
                                           key);
                }
                break;
            }
            case Section::Ppargo: {
                if (key == "packages") {
                    TRY_void(ensure_unique(section, key));
                    auto parsed_bool = TRY(parse_bool(value));
                    manifest.features.packages = parsed_bool;
                } else if (key == "package_manager" || key == "package-manager") {
                    TRY_void(ensure_unique(section, "package_manager"));
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            "Manifest Error: package_manager must be a string.");
                    }
                    manifest.features.package_manager = unquote(value);
                } else if (key == "vcpkg_root" || key == "vcpkg-root") {
                    TRY_void(ensure_unique(section, "vcpkg_root"));
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            "Manifest Error: vcpkg_root must be a string.");
                    }
                    manifest.features.vcpkg_root = std::filesystem::path(unquote(value));
                    manifest.features.has_vcpkg_root = true;
                } else {
                    return std::unexpected(
                        "Manifest Error: Unknown key in [package.metadata.ppargo]: " +
                        key);
                }
                break;
            }
            case Section::Toolchain: {
                if (key == "compiler") {
                    TRY_void(ensure_unique(section, key));
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            "Manifest Error: [toolchain].compiler must be a string.");
                    }
                    manifest.toolchain.compiler = unquote(value);
                } else if (key == "linker") {
                    TRY_void(ensure_unique(section, key));
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            "Manifest Error: [toolchain].linker must be a string.");
                    }
                    manifest.toolchain.linker = unquote(value);
                } else {
                    return std::unexpected(
                        "Manifest Error: Unknown key in [toolchain]: " + key);
                }
                break;
            }
            case Section::LegacyFeatures: {
                if (key == "packages") {
                    TRY_void(ensure_unique(section, key));
                    auto parsed_bool = TRY(parse_bool(value));
                    manifest.features.packages = parsed_bool;
                } else if (key == "package_manager" || key == "package-manager") {
                    TRY_void(ensure_unique(section, "package_manager"));
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            "Manifest Error: package_manager must be a string.");
                    }
                    manifest.features.package_manager = unquote(value);
                } else if (key == "vcpkg_root" || key == "vcpkg-root") {
                    TRY_void(ensure_unique(section, "vcpkg_root"));
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            "Manifest Error: vcpkg_root must be a string.");
                    }
                    manifest.features.vcpkg_root = std::filesystem::path(unquote(value));
                    manifest.features.has_vcpkg_root = true;
                } else {
                    return std::unexpected(
                        "Manifest Error: Unknown key in [features]: " + key);
                }
                break;
            }
            case Section::Build: {
                if (key == "source_dir" || key == "source-dir") {
                    TRY_void(ensure_unique(section, "source_dir"));
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            "Manifest Error: source_dir must be a string.");
                    }
                    manifest.build.source_dir = std::filesystem::path(unquote(value));
                } else if (key == "output_dir" || key == "output-dir") {
                    TRY_void(ensure_unique(section, "output_dir"));
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            "Manifest Error: output_dir must be a string.");
                    }
                    manifest.build.output_dir = std::filesystem::path(unquote(value));
                } else if (key == "binary_name" || key == "binary-name") {
                    TRY_void(ensure_unique(section, "binary_name"));
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            "Manifest Error: binary_name must be a string.");
                    }
                    manifest.build.binary_name = unquote(value);
                } else if (key == "include_dirs" || key == "include-dirs") {
                    TRY_void(ensure_unique(section, "include_dirs"));
                    manifest.build.include_dirs.clear();
                    auto include_entries = TRY(parse_string_array(value));
                    for (const auto& entry : include_entries) {
                        manifest.build.include_dirs.emplace_back(entry);
                    }
                } else if (key == "exclude") {
                    TRY_void(ensure_unique(section, "exclude"));
                    auto exclude_entries = TRY(parse_string_array(value));
                    manifest.build.exclude = std::move(exclude_entries);
                } else {
                    return std::unexpected("Manifest Error: Unknown key in [build]: " +
                                           key);
                }
                break;
            }
            case Section::Dependencies: {
                TRY_void(ensure_unique(section, key));
                if (!is_quoted(value)) {
                    return std::unexpected(
                        "Manifest Error: dependency versions must be strings.");
                }
                manifest.dependencies[key] = unquote(value);
                break;
            }
            case Section::None:
                return std::unexpected(
                    "Manifest Error: Key/value encountered outside a supported section: " +
                    key);
        }
    }

    return manifest;
}

auto save_manifest(const std::filesystem::path& path,
                   const Manifest& manifest) -> util::Status {
    auto toml = to_toml(manifest);
    if (!toml) {
        return std::unexpected(toml.error());
    }
    return util::fs::atomic_write_text_result(path, *toml);
}

auto to_toml(const Manifest& manifest) -> util::Result<std::string> {
    std::ostringstream out;

    out << "[package]\n";
    out << "name = " << toml_quote(manifest.package.name) << "\n";
    out << "version = " << toml_quote(manifest.package.version) << "\n";
    out << "edition = " << toml_quote(manifest.package.edition) << "\n\n";

    out << "[dependencies]\n";
    for (const auto& [name, version] : manifest.dependencies) {
        out << name << " = " << toml_quote(version) << "\n";
    }
    out << "\n";

    out << "[toolchain]\n";
    out << "compiler = " << toml_quote(manifest.toolchain.compiler) << "\n";
    out << "linker = " << toml_quote(manifest.toolchain.linker) << "\n\n";

    out << "[features]\n";
    out << "packages = " << (manifest.features.packages ? "true" : "false") << "\n";
    out << "package_manager = " << toml_quote(manifest.features.package_manager) << "\n";
    if (manifest.features.has_vcpkg_root) {
        out << "vcpkg_root = "
            << toml_quote(path_as_toml_string(manifest.features.vcpkg_root)) << "\n";
    }
    out << "\n";

    const bool emit_build =
        manifest.build.source_dir != std::filesystem::path("src") ||
        manifest.build.include_dirs !=
            std::vector<std::filesystem::path>{std::filesystem::path("src"),
                                               std::filesystem::path("include")} ||
        manifest.build.exclude != std::vector<std::string>{"target/**"} ||
        manifest.build.output_dir != std::filesystem::path("target/cpp") ||
        !manifest.build.binary_name.empty();

    if (emit_build) {
        out << "[build]\n";
        out << "source_dir = " << toml_quote(path_as_toml_string(manifest.build.source_dir))
            << "\n";

        std::vector<std::string> include_dirs;
        include_dirs.reserve(manifest.build.include_dirs.size());
        for (const auto& dir : manifest.build.include_dirs) {
            include_dirs.push_back(path_as_toml_string(dir));
        }
        out << "include_dirs = " << array_to_toml(include_dirs) << "\n";
        out << "exclude = " << array_to_toml(manifest.build.exclude) << "\n";
        out << "output_dir = " << toml_quote(path_as_toml_string(manifest.build.output_dir))
            << "\n";
        if (!manifest.build.binary_name.empty()) {
            out << "binary_name = " << toml_quote(manifest.build.binary_name) << "\n";
        }
    }

    return out.str();
}

}  // namespace core



