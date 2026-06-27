#include <cctype>
#include <charconv>
#include <format>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

#include "core/manifest.hpp"
#include "util/text.hpp"

namespace {

enum class Section {
    None,
    Package,
    Toolchain,
    Features,
    Build,
    Dependencies,
    DevDependencies,
    BuildDependencies,
    Registries,
    Workspace,
};

auto strip_comment(std::string_view line) -> std::string {
    bool in_single = false;
    bool in_double = false;
    bool escaped = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_double) {
            escaped = true;
            continue;
        }
        if (ch == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }
        if (ch == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }
        if (ch == '#' && !in_single && !in_double) {
            return std::string(line.substr(0, i));
        }
    }

    return std::string(line);
}

auto is_quoted(std::string_view input) -> bool {
    const auto value = util::text::trim_ascii_copy(input);
    if (value.size() < 2) {
        return false;
    }
    return (value.front() == '"' && value.back() == '"') ||
           (value.front() == '\'' && value.back() == '\'');
}

auto unquote(std::string_view input) -> std::string {
    const auto value = util::text::trim_ascii_copy(input);
    if (!is_quoted(value)) {
        return value;
    }

    const auto inner = value.substr(1, value.size() - 2);
    if (value.front() == '\'') {
        return std::string(inner);
    }

    std::string result;
    result.reserve(inner.size());

    bool escaped = false;
    for (char ch : inner) {
        if (!escaped) {
            if (ch == '\\') {
                escaped = true;
            } else {
                result.push_back(ch);
            }
            continue;
        }

        switch (ch) {
            case '\\':
                result.push_back('\\');
                break;
            case '"':
                result.push_back('"');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                result.push_back(ch);
                break;
        }
        escaped = false;
    }
    if (escaped) {
        result.push_back('\\');
    }

    return result;
}

auto parse_non_negative_int(std::string_view input, std::string_view key)
    -> util::Result<int> {
    const auto value = util::text::trim_ascii_copy(input);
    if (value.empty()) {
        return std::unexpected(
            util::make_error(std::format("{} must not be empty.", key)));
    }

    int parsed = 0;
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end || parsed < 0) {
        return std::unexpected(util::make_error(
            std::format("{} must be a non-negative integer.", key)));
    }
    return parsed;
}

auto parse_positive_int(std::string_view input, std::string_view key)
    -> util::Result<int> {
    const auto parsed = GUARD(parse_non_negative_int(input, key));
    if (parsed <= 0) {
        return std::unexpected(
            util::make_error(std::format("{} must be greater than 0.", key)));
    }
    return parsed;
}

auto parse_probability(std::string_view input, std::string_view key)
    -> util::Result<double> {
    const auto value = util::text::trim_ascii_copy(input);
    if (value.empty()) {
        return std::unexpected(
            util::make_error(std::format("{} must not be empty.", key)));
    }

    double parsed = 0.0;
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end || parsed < 0.0 || parsed > 1.0) {
        return std::unexpected(util::make_error(
            std::format("{} must be a number between 0.0 and 1.0.", key)));
    }
    return parsed;
}

auto parse_string_array(std::string_view input)
    -> util::Result<std::vector<std::string>> {
    auto value = util::text::trim_ascii_copy(input);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        return std::unexpected(
            util::make_error("Invalid TOML array value: " + value));
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
            const auto token = util::text::trim_ascii_copy(current);
            if (!token.empty()) {
                if (!is_quoted(token)) {
                    return std::unexpected(util::make_error(
                        "Invalid TOML array value (non-string element): " +
                        std::string(input)));
                }
                result.push_back(unquote(token));
            }
            current.clear();
            continue;
        }

        current.push_back(ch);
    }

    if (in_single || in_double || escaped) {
        return std::unexpected(util::make_error(
            "Invalid TOML array value (unterminated string): " +
            std::string(input)));
    }

    const auto token = util::text::trim_ascii_copy(current);
    if (!token.empty()) {
        if (!is_quoted(token)) {
            return std::unexpected(util::make_error(
                "Invalid TOML array value (non-string element): " +
                std::string(input)));
        }
        result.push_back(unquote(token));
    }

    return result;
}

auto normalize_key(std::string key) -> std::string {
    for (char& ch : key) {
        if (ch == '-') {
            ch = '_';
        }
    }
    return key;
}

auto parse_inline_table(std::string_view input)
    -> util::Result<std::map<std::string, std::string>> {
    auto value = util::text::trim_ascii_copy(input);
    if (value.size() < 2 || value.front() != '{' || value.back() != '}') {
        return std::unexpected(
            util::make_error("Invalid inline table: " + value));
    }

    value = value.substr(1, value.size() - 2);
    std::map<std::string, std::string> result;

    std::string current;
    bool in_single = false;
    bool in_double = false;
    bool escaped = false;
    int bracket_depth = 0;

    const auto flush_token = [&result](std::string_view token) -> util::Status {
        const auto entry = util::text::trim_ascii_copy(token);
        if (entry.empty()) {
            return util::Ok;
        }

        const auto eq = entry.find('=');
        if (eq == std::string::npos) {
            return std::unexpected(
                util::make_error("Invalid inline table entry: " + entry));
        }

        auto key =
            normalize_key(util::text::trim_ascii_copy(entry.substr(0, eq)));
        const auto val = util::text::trim_ascii_copy(entry.substr(eq + 1));

        if (key.empty()) {
            return std::unexpected(
                util::make_error("Empty key in inline table."));
        }
        if (result.contains(key)) {
            return std::unexpected(
                util::make_error("Duplicate key in inline table: " + key));
        }

        result[key] = val;
        return util::Ok;
    };

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

        if (!in_single && !in_double) {
            if (ch == '[') {
                ++bracket_depth;
            } else if (ch == ']') {
                --bracket_depth;
                if (bracket_depth < 0) {
                    return std::unexpected(
                        util::make_error("Malformed inline table array."));
                }
            } else if (ch == ',' && bracket_depth == 0) {
                GUARD(flush_token(current));
                current.clear();
                continue;
            }
        }

        current.push_back(ch);
    }

    if (in_single || in_double || escaped || bracket_depth != 0) {
        return std::unexpected(util::make_error(
            "Unterminated inline table value: " + std::string(input)));
    }

    GUARD(flush_token(current));
    return result;
}

auto validate_semver_requirement(std::string_view value) -> util::Status {
    static const std::regex pattern(R"(^[A-Za-z0-9^~<>=*., +\-]+$)");
    if (value.empty()) {
        return util::Ok;
    }
    if (!std::regex_match(value.begin(), value.end(), pattern)) {
        return std::unexpected(util::make_error(
            "Invalid version requirement: " + std::string(value)));
    }
    return util::Ok;
}

auto validate_registry_name(std::string_view value) -> util::Status {
    static const std::regex pattern(R"(^[A-Za-z0-9][A-Za-z0-9_.-]*$)");
    if (!std::regex_match(value.begin(), value.end(), pattern)) {
        return std::unexpected(
            util::make_error("Invalid registry name: " + std::string(value)));
    }
    return util::Ok;
}

auto validate_module_extension(std::string_view value) -> util::Status {
    if (value.empty() || value.front() != '.') {
        return std::unexpected(util::make_error(
            "module_interface_exts entries must start with '.'."));
    }
    return util::Ok;
}

auto supports_modules_edition(std::string_view edition) -> bool {
    return edition == "cpp20" || edition == "cpp23" || edition == "cpp26";
}

auto contains_path_traversal(const std::filesystem::path& value) -> bool {
    for (const auto& component : value) {
        if (component == "..") {
            return true;
        }
    }
    return false;
}

auto parse_dependency_spec(std::string_view dep_name, std::string_view value)
    -> util::Result<core::DependencySpec> {
    core::DependencySpec spec;

    if (is_quoted(value)) {
        spec.version = unquote(value);
        GUARD(validate_semver_requirement(spec.version));
        return spec;
    }

    auto table = GUARD(parse_inline_table(value));
    for (const auto& [key, raw] : table) {
        if (key == "version") {
            if (!is_quoted(raw)) {
                return std::unexpected(util::make_error(
                    "dependency.version must be a string for '" +
                    std::string(dep_name) + "'."));
            }
            spec.version = unquote(raw);
            GUARD(validate_semver_requirement(spec.version));
        } else if (key == "path") {
            if (!is_quoted(raw)) {
                return std::unexpected(
                    util::make_error("dependency.path must be a string for '" +
                                     std::string(dep_name) + "'."));
            }
            spec.path = std::filesystem::path(unquote(raw));
        } else if (key == "git") {
            if (!is_quoted(raw)) {
                return std::unexpected(
                    util::make_error("dependency.git must be a string for '" +
                                     std::string(dep_name) + "'."));
            }
            spec.git = unquote(raw);
        } else if (key == "rev") {
            if (!is_quoted(raw)) {
                return std::unexpected(
                    util::make_error("dependency.rev must be a string for '" +
                                     std::string(dep_name) + "'."));
            }
            spec.rev = unquote(raw);
        } else if (key == "registry") {
            if (!is_quoted(raw)) {
                return std::unexpected(util::make_error(
                    "dependency.registry must be a string for '" +
                    std::string(dep_name) + "'."));
            }
            spec.registry = unquote(raw);
            GUARD(validate_registry_name(spec.registry));
        } else if (key == "features") {
            spec.features = GUARD(parse_string_array(raw));
        } else if (key == "optional") {
            spec.optional = GUARD(util::text::parse_bool_literal(raw));
        } else if (key == "default_features") {
            spec.default_features = GUARD(util::text::parse_bool_literal(raw));
        } else {
            return std::unexpected(
                util::make_error("Unknown dependency key '" + key + "' in '" +
                                 std::string(dep_name) + "'."));
        }
    }

    const bool has_path = !spec.path.empty();
    const bool has_git = !spec.git.empty();
    if (has_path && has_git) {
        return std::unexpected(
            util::make_error("dependency '" + std::string(dep_name) +
                             "' cannot declare both path and git sources."));
    }

    if (has_path) {
        if (contains_path_traversal(spec.path)) {
            return std::unexpected(
                util::make_error("dependency.path must not contain '..' for '" +
                                 std::string(dep_name) + "'."));
        }
        spec.source = core::DependencySource::Path;
    } else if (has_git) {
        spec.source = core::DependencySource::Git;
        if (!spec.registry.empty()) {
            return std::unexpected(
                util::make_error("dependency '" + std::string(dep_name) +
                                 "' cannot combine git and registry keys."));
        }
    } else {
        spec.source = core::DependencySource::Registry;
        if (spec.version.empty()) {
            return std::unexpected(util::make_error(
                "dependency '" + std::string(dep_name) +
                "' must define a version for registry source."));
        }
    }

    if (!has_git && !spec.rev.empty()) {
        return std::unexpected(
            util::make_error("dependency '" + std::string(dep_name) +
                             "' uses rev without git source."));
    }

    return spec;
}

auto parse_registry_config(std::string_view name, std::string_view value)
    -> util::Result<core::RegistryConfig> {
    core::RegistryConfig config;
    auto table = GUARD(parse_inline_table(value));
    for (const auto& [key, raw] : table) {
        if (key == "index") {
            if (!is_quoted(raw)) {
                return std::unexpected(
                    util::make_error("registry.index must be a string for '" +
                                     std::string(name) + "'."));
            }
            config.index = unquote(raw);
        } else if (key == "api") {
            if (!is_quoted(raw)) {
                return std::unexpected(
                    util::make_error("registry.api must be a string for '" +
                                     std::string(name) + "'."));
            }
            config.api = unquote(raw);
        } else {
            return std::unexpected(util::make_error("Unknown key '" + key +
                                                    "' in [registries]." +
                                                    std::string(name)));
        }
    }

    if (config.index.empty()) {
        return std::unexpected(util::make_error("[registries]." +
                                                std::string(name) +
                                                " must provide an index URL."));
    }

    return config;
}

}  // namespace

namespace core {

auto load_manifest(const std::filesystem::path& path)
    -> util::Result<Manifest> {
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::unexpected(
            util::make_error("Failed to open manifest: " + path.string()));
    }

    Manifest manifest =
        default_manifest(path.parent_path().filename().string().empty()
                             ? "ppargo"
                             : path.parent_path().filename().string());
    manifest.package_defined = false;

    Section section = Section::None;
    std::unordered_map<int, std::set<std::string>> seen_keys;
    bool package_manager_seen = false;
    bool packages_seen = false;

    const auto ensure_unique = [&seen_keys](
                                   Section current_section,
                                   std::string_view key) -> util::Status {
        auto& keys = seen_keys[static_cast<int>(current_section)];
        if (!keys.insert(std::string(key)).second) {
            return std::unexpected(
                util::make_error("Duplicate key '" + std::string(key) + "'."));
        }
        return util::Ok;
    };

    bool first_line = true;
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

        line = util::text::trim_ascii_copy(strip_comment(line));
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            const auto header =
                util::text::trim_ascii_copy(line.substr(1, line.size() - 2));

            if (header.rfind("package.metadata.ppargo", 0) == 0) {
                return std::unexpected(
                    util::make_error("Legacy package.metadata.ppargo sections "
                                     "are no longer supported. "
                                     "Use top-level [toolchain], [features], "
                                     "[build], [dependencies], [registries]."));
            }

            if (header == "package") {
                section = Section::Package;
                manifest.package_defined = true;
            } else if (header == "toolchain") {
                section = Section::Toolchain;
            } else if (header == "features") {
                section = Section::Features;
            } else if (header == "build") {
                section = Section::Build;
            } else if (header == "dependencies") {
                section = Section::Dependencies;
            } else if (header == "dev-dependencies") {
                section = Section::DevDependencies;
            } else if (header == "build-dependencies") {
                section = Section::BuildDependencies;
            } else if (header == "registries") {
                section = Section::Registries;
            } else if (header == "workspace") {
                section = Section::Workspace;
                manifest.workspace.enabled = true;
            } else {
                section = Section::None;
            }
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            return std::unexpected(
                util::make_error("Invalid line (missing '='): " + line));
        }

        const auto key =
            normalize_key(util::text::trim_ascii_copy(line.substr(0, eq)));
        const auto value = util::text::trim_ascii_copy(line.substr(eq + 1));

        if (key.empty()) {
            return std::unexpected(util::make_error("Empty key in manifest."));
        }

        switch (section) {
            case Section::Package: {
                GUARD(ensure_unique(section, key));
                if (key == "name") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error(
                            "[package].name must be a string."));
                    }
                    manifest.package.name = unquote(value);
                } else if (key == "version") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error(
                            "[package].version must be a string."));
                    }
                    manifest.package.version = unquote(value);
                } else if (key == "edition") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error(
                            "[package].edition must be a string."));
                    }
                    manifest.package.edition = unquote(value);
                } else {
                    return std::unexpected(
                        util::make_error("Unknown key in [package]: " + key));
                }
                break;
            }
            case Section::Toolchain: {
                GUARD(ensure_unique(section, key));
                if (!is_quoted(value)) {
                    return std::unexpected(util::make_error(
                        "[toolchain]." + key + " must be a string."));
                }
                if (key == "compiler") {
                    manifest.toolchain.compiler = unquote(value);
                } else {
                    return std::unexpected(
                        util::make_error("Unknown key in [toolchain]: " + key));
                }
                break;
            }
            case Section::Features: {
                GUARD(ensure_unique(section, key));
                if (key == "packages") {
                    manifest.features.packages =
                        GUARD(util::text::parse_bool_literal(value));
                    packages_seen = true;
                } else if (key == "package_manager") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error(
                            "package_manager must be a string."));
                    }
                    manifest.features.package_manager = unquote(value);
                    package_manager_seen = true;
                } else if (key == "vcpkg_root") {
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            util::make_error("vcpkg_root must be a string."));
                    }
                    manifest.features.vcpkg_root =
                        std::filesystem::path(unquote(value));
                } else {
                    manifest.features.package_features[key] =
                        GUARD(parse_string_array(value));
                }
                break;
            }
            case Section::Build: {
                GUARD(ensure_unique(section, key));
                if (key == "source_dir") {
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            util::make_error("source_dir must be a string."));
                    }
                    manifest.build.source_dir =
                        std::filesystem::path(unquote(value));
                } else if (key == "include_dirs") {
                    manifest.build.include_dirs.clear();
                    const auto include_items = GUARD(parse_string_array(value));
                    for (const auto& item : include_items) {
                        manifest.build.include_dirs.emplace_back(item);
                    }
                } else if (key == "exclude") {
                    manifest.build.exclude = GUARD(parse_string_array(value));
                } else if (key == "output_dir") {
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            util::make_error("output_dir must be a string."));
                    }
                    manifest.build.output_dir =
                        std::filesystem::path(unquote(value));
                } else if (key == "binary_name") {
                    if (!is_quoted(value)) {
                        return std::unexpected(
                            util::make_error("binary_name must be a string."));
                    }
                    manifest.build.binary_name = unquote(value);
                } else if (key == "modules") {
                    manifest.build.modules =
                        GUARD(util::text::parse_bool_literal(value));
                } else if (key == "module_interface_exts") {
                    auto exts = GUARD(parse_string_array(value));
                    if (exts.empty()) {
                        return std::unexpected(util::make_error(
                            "module_interface_exts must not be empty."));
                    }
                    for (const auto& ext : exts) {
                        GUARD(validate_module_extension(ext));
                    }
                    manifest.build.module_interface_exts = std::move(exts);
                } else if (key == "module_output_dir") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error(
                            "module_output_dir must be a string."));
                    }
                    manifest.build.module_output_dir =
                        std::filesystem::path(unquote(value));
                } else if (key == "aggressive_tu_threshold") {
                    manifest.build.aggressive_tu_threshold =
                        GUARD(parse_non_negative_int(value, key));
                } else if (key == "aggressive_stale_threshold") {
                    manifest.build.aggressive_stale_threshold =
                        GUARD(parse_non_negative_int(value, key));
                } else if (key == "pch_scan_lines") {
                    manifest.build.pch_scan_lines =
                        GUARD(parse_non_negative_int(value, key));
                } else if (key == "pch_frequency_threshold") {
                    manifest.build.pch_frequency_threshold =
                        GUARD(parse_probability(value, key));
                } else if (key == "pch_max_headers") {
                    manifest.build.pch_max_headers =
                        GUARD(parse_non_negative_int(value, key));
                } else if (key == "depscan_timeout_ms") {
                    manifest.build.depscan_timeout_ms =
                        GUARD(parse_positive_int(value, key));
                } else {
                    return std::unexpected(
                        util::make_error("Unknown key in [build]: " + key));
                }
                break;
            }
            case Section::Dependencies:
            case Section::DevDependencies:
            case Section::BuildDependencies: {
                GUARD(ensure_unique(section, key));
                auto spec = GUARD(parse_dependency_spec(key, value));
                if (section == Section::Dependencies) {
                    manifest.dependencies[key] = std::move(spec);
                } else if (section == Section::DevDependencies) {
                    manifest.dev_dependencies[key] = std::move(spec);
                } else {
                    manifest.build_dependencies[key] = std::move(spec);
                }
                break;
            }
            case Section::Registries: {
                GUARD(ensure_unique(section, key));
                auto registry = GUARD(parse_registry_config(key, value));
                manifest.registries[key] = std::move(registry);
                break;
            }
            case Section::Workspace: {
                GUARD(ensure_unique(section, key));
                if (key == "members") {
                    manifest.workspace.members.clear();
                    const auto members = GUARD(parse_string_array(value));
                    for (const auto& member : members) {
                        manifest.workspace.members.emplace_back(member);
                    }
                } else if (key == "exclude") {
                    manifest.workspace.exclude.clear();
                    const auto excludes = GUARD(parse_string_array(value));
                    for (const auto& exclude : excludes) {
                        manifest.workspace.exclude.emplace_back(exclude);
                    }
                } else {
                    return std::unexpected(
                        util::make_error("Unknown key in [workspace]: " + key));
                }
                break;
            }
            case Section::None:
                return std::unexpected(util::make_error(
                    "Key/value encountered outside a supported section: " +
                    key));
        }
    }

    if (manifest.build.modules &&
        !supports_modules_edition(manifest.package.edition)) {
        return std::unexpected(
            util::make_error("[build].modules=true requires package.edition to "
                             "be cpp20, cpp23, or cpp26."));
    }

    if (!packages_seen && package_manager_seen &&
        !manifest.features.package_manager.empty()) {
        manifest.features.packages = true;
    }
    if (manifest.features.packages &&
        manifest.features.package_manager.empty()) {
        return std::unexpected(
            util::make_error("[features].packages=true requires a non-empty "
                             "[features].package_manager."));
    }

    if (!manifest.package_defined && !manifest.workspace.enabled) {
        manifest.package_defined = true;
    }

    return manifest;
}

}  // namespace core
