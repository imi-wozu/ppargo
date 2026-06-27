#include "core/lockfile.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>

#include "util/containers.hpp"
#include "util/text.hpp"

namespace {

auto is_quoted(std::string_view  input) -> bool {
    const auto value = util::text::trim_ascii_copy(input);
    return value.size() >= 2 && value.front() == '"' && value.back() == '"';
}

auto unquote(std::string_view  input) -> std::string {
    const auto value = util::text::trim_ascii_copy(input);
    if (!is_quoted(value)) {
        return std::string(value);
    }

    const auto inner = std::string_view(value).substr(1, value.size() - 2);
    std::string unescaped;
    unescaped.reserve(inner.size());
    for (std::size_t index = 0; index < inner.size(); ++index) {
        const char ch = inner[index];
        if (ch != '\\' || index + 1 >= inner.size()) {
            unescaped.push_back(ch);
            continue;
        }

        const char escaped = inner[index + 1];
        if (escaped == '\\' || escaped == '"') {
            unescaped.push_back(escaped);
            ++index;
            continue;
        }

        unescaped.push_back(ch);
    }
    return unescaped;
}

auto parse_string_array(std::string_view  input)
    -> util::Result<std::vector<std::string>> {
    auto value = util::text::trim_ascii_copy(input);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        return std::unexpected(util::make_error("Invalid array value: " + value));
    }

    value = value.substr(1, value.size() - 2);
    std::vector<std::string> result;

    std::string token;
    bool in_quote = false;
    bool escaped = false;

    for (char ch : value) {
        if (escaped) {
            token.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_quote) {
            token.push_back(ch);
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_quote = !in_quote;
            token.push_back(ch);
            continue;
        }
        if (ch == ',' && !in_quote) {
            const auto item = util::text::trim_ascii_copy(token);
            if (!item.empty()) {
                if (!is_quoted(item)) {
                    return std::unexpected(util::make_error("Array item must be string."));
                }
                result.push_back(unquote(item));
            }
            token.clear();
            continue;
        }
        token.push_back(ch);
    }

    if (in_quote || escaped) {
        return std::unexpected(util::make_error("Unterminated string array."));
    }

    const auto item = util::text::trim_ascii_copy(token);
    if (!item.empty()) {
        if (!is_quoted(item)) {
            return std::unexpected(util::make_error("Array item must be string."));
        }
        result.push_back(unquote(item));
    }

    return result;
}

auto parse_int(std::string_view  input) -> util::Result<int> {
    const auto value = util::text::trim_ascii_copy(input);
    int parsed = 0;
    const auto begin = value.data();
    const auto end = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) {
        return std::unexpected(util::make_error("Invalid integer value: " + value));
    }
    return parsed;
}

auto module_support_from_string(std::string_view  value)
    -> util::Result<core::LockModuleSupport> {
    if (value == "none") {
        return core::LockModuleSupport::None;
    }
    if (value == "named") {
        return core::LockModuleSupport::Named;
    }
    if (value == "header-units") {
        return core::LockModuleSupport::HeaderUnits;
    }
    return std::unexpected(util::make_error("Invalid module_support value: " + std::string(value)));
}

auto finalize_package(core::LockPackage& package) -> util::Status {
    if (package.name.empty()) {
        return std::unexpected(util::make_error("package.name is required."));
    }
    if (package.version.empty()) {
        return std::unexpected(util::make_error("package.version is required."));
    }
    if (package.source.empty()) {
        return std::unexpected(util::make_error("package.source is required."));
    }
    if (package.artifact.empty() && package.source.rfind("registry+", 0) == 0) {
        package.artifact = package.name + "-" + package.version + ".crate";
    }

    const auto expected_id =
        core::make_lock_package_id(package.name, package.version, package.source);
    if (package.id.empty()) {
        package.id = expected_id;
    } else if (package.id != expected_id) {
        return std::unexpected(util::make_error("package.id does not match name/version/source."));
    }

    util::sort_and_deduplicate(package.dependencies);
    util::sort_and_deduplicate(package.unresolved_dependencies);
    util::sort_and_deduplicate(package.active_features);
    util::sort_and_deduplicate(package.bmi_include_dirs);
    util::sort_and_deduplicate(package.exported_modules);
    return util::Ok;
}

void apply_legacy_dependency_compatibility(core::Lockfile& lockfile) {
    if (lockfile.version >= core::kCurrentLockfileVersion) {
        return;
    }

    for (auto& package : lockfile.packages) {
        package.unresolved_dependencies.insert(package.unresolved_dependencies.end(),
                                               package.dependencies.begin(),
                                               package.dependencies.end());
        package.dependencies.clear();
        util::sort_and_deduplicate(package.unresolved_dependencies);
    }
}

auto validate_lockfile(core::Lockfile& lockfile) -> util::Status {
    if (lockfile.version <= 0) {
        return std::unexpected(util::make_error("version must be positive."));
    }
    if (lockfile.manifest_fingerprint.empty()) {
        return std::unexpected(util::make_error("metadata.manifest_fingerprint is required."));
    }

    apply_legacy_dependency_compatibility(lockfile);

    std::unordered_set<std::string> ids;
    for (auto& package : lockfile.packages) {
        GUARD(finalize_package(package));
        if (!ids.insert(package.id).second) {
            return std::unexpected(util::make_error("Duplicate package id: " +
                                       package.id));
        }
    }
    return util::Ok;
}

}  // namespace

namespace core {

auto load_lockfile(const std::filesystem::path& path) -> util::Result<Lockfile> {
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::unexpected(util::make_error("Failed to open lockfile: " + path.string()));
    }

    enum class Section {
        TopLevel,
        Metadata,
        Package,
    };

    Lockfile lockfile;
    LockPackage current;
    Section section = Section::TopLevel;
    bool has_package = false;
    bool saw_version = false;

    std::string line;
    while (std::getline(input, line)) {
        line = util::text::trim_ascii_copy(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        if (line == "[metadata]") {
            if (has_package) {
                GUARD(finalize_package(current));
                lockfile.packages.push_back(std::move(current));
                current = LockPackage{};
                has_package = false;
            }
            section = Section::Metadata;
            continue;
        }

        if (line == "[[package]]") {
            if (has_package) {
                GUARD(finalize_package(current));
                lockfile.packages.push_back(std::move(current));
                current = LockPackage{};
            }
            section = Section::Package;
            has_package = true;
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            return std::unexpected(util::make_error("Invalid line: " + line));
        }

        const auto key = util::text::trim_ascii_copy(line.substr(0, eq));
        const auto value = util::text::trim_ascii_copy(line.substr(eq + 1));

        switch (section) {
            case Section::TopLevel: {
                if (key != "version") {
                    return std::unexpected(util::make_error("key/value outside section: " + key));
                }
                lockfile.version = GUARD(parse_int(value));
                saw_version = true;
                break;
            }
            case Section::Metadata:
                if (key == "manifest_fingerprint") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error("metadata.manifest_fingerprint must be string."));
                    }
                    lockfile.manifest_fingerprint = unquote(value);
                } else if (key == "package_manager") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error("metadata.package_manager must be string."));
                    }
                    lockfile.package_manager = unquote(value);
                } else if (key == "root_features") {
                    lockfile.root_features = GUARD(parse_string_array(value));
                } else if (key == "all_features") {
                    lockfile.all_features =
                        GUARD(util::text::parse_bool_literal(value));
                } else if (key == "no_default_features") {
                    lockfile.no_default_features =
                        GUARD(util::text::parse_bool_literal(value));
                } else if (key == "module_mode_enabled") {
                    lockfile.module_mode_enabled =
                        GUARD(util::text::parse_bool_literal(value));
                } else if (key == "compiler") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error("metadata.compiler must be string."));
                    }
                    lockfile.compiler = unquote(value);
                } else if (key == "scan_deps_fingerprint") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error("metadata.scan_deps_fingerprint must be string."));
                    }
                    lockfile.scan_deps_fingerprint = unquote(value);
                } else {
                    return std::unexpected(util::make_error("Unknown metadata key: " + key));
                }
                break;
            case Section::Package:
                if (key == "id") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error("package.id must be string."));
                    }
                    current.id = unquote(value);
                } else if (key == "name") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error("package.name must be string."));
                    }
                    current.name = unquote(value);
                } else if (key == "version") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error("package.version must be string."));
                    }
                    current.version = unquote(value);
                } else if (key == "source") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error("package.source must be string."));
                    }
                    current.source = unquote(value);
                } else if (key == "checksum") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error("package.checksum must be string."));
                    }
                    current.checksum = unquote(value);
                } else if (key == "artifact") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error("package.artifact must be string."));
                    }
                    current.artifact = unquote(value);
                } else if (key == "dependencies") {
                    current.dependencies = GUARD(parse_string_array(value));
                } else if (key == "unresolved_dependencies") {
                    current.unresolved_dependencies = GUARD(parse_string_array(value));
                } else if (key == "active_features") {
                    current.active_features = GUARD(parse_string_array(value));
                } else if (key == "module_support") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error("package.module_support must be string."));
                    }
                    current.module_support = GUARD(module_support_from_string(unquote(value)));
                } else if (key == "module_map_path") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error("package.module_map_path must be string."));
                    }
                    current.module_map_path = unquote(value);
                } else if (key == "bmi_include_dirs") {
                    current.bmi_include_dirs = GUARD(parse_string_array(value));
                } else if (key == "exported_modules") {
                    current.exported_modules = GUARD(parse_string_array(value));
                } else if (key == "module_metadata_fingerprint") {
                    if (!is_quoted(value)) {
                        return std::unexpected(util::make_error("package.module_metadata_fingerprint must be string."));
                    }
                    current.module_metadata_fingerprint = unquote(value);
                } else {
                    return std::unexpected(util::make_error("Unknown package key: " + key));
                }
                break;
        }
    }

    if (has_package) {
        GUARD(finalize_package(current));
        lockfile.packages.push_back(std::move(current));
    }
    if (!saw_version) {
        lockfile.version = 1;
    }

    GUARD(validate_lockfile(lockfile));
    return lockfile;
}

}  // namespace core




