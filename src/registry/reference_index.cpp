#include "registry/reference_index.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <format>
#include <optional>
#include <regex>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "package/home.hpp"
#include "registry/reference_support.hpp"
#include "util/process.hpp"
#include "util/text.hpp"

namespace registry::reference::detail {

namespace {

auto parse_semver_component(std::string_view value, std::string_view  full_version)
    -> util::Result<int> {
    int parsed = 0;
    const auto begin = value.data();
    const auto end = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) {
        return std::unexpected(util::make_error("Invalid semver string: " + std::string(full_version)));
    }
    return parsed;
}

auto parse_semver(std::string_view  version)
    -> util::Result<std::tuple<int, int, int, std::string>> {
    const auto hash_pos = version.find('#');
    const auto core =
        hash_pos == std::string::npos ? version : version.substr(0, hash_pos);
    const auto build = hash_pos == std::string::npos ? std::string{} : std::string(version.substr(hash_pos + 1));

    const auto first_dot = core.find('.');
    const auto second_dot =
        first_dot == std::string::npos ? std::string::npos
                                       : core.find('.', first_dot + 1);
    if (first_dot == std::string::npos || second_dot == std::string::npos ||
        core.find('.', second_dot + 1) != std::string::npos) {
        return std::unexpected(util::make_error("Invalid semver string: " + std::string(version)));
    }

    const std::string_view major_text(core.data(), first_dot);
    const std::string_view minor_text(core.data() + first_dot + 1,
                                      second_dot - first_dot - 1);
    const std::string_view patch_text(core.data() + second_dot + 1,
                                      core.size() - second_dot - 1);

    const auto major = GUARD(parse_semver_component(major_text, version));
    const auto minor = GUARD(parse_semver_component(minor_text, version));
    const auto patch = GUARD(parse_semver_component(patch_text, version));
    return std::tuple<int, int, int, std::string>{major, minor, patch, build};
}

auto skip_json_ws(std::string_view json, std::size_t index) -> std::size_t {
    while (index < json.size() &&
           std::isspace(static_cast<unsigned char>(json[index])) != 0) {
        ++index;
    }
    return index;
}

auto find_json_value_start(std::string_view json, std::string_view field)
    -> std::optional<std::size_t> {
    const auto needle = std::string{"\""} + std::string(field) + "\"";
    auto position = json.find(needle);
    while (position != std::string_view::npos) {
        position += needle.size();
        position = skip_json_ws(json, position);
        if (position < json.size() && json[position] == ':') {
            return skip_json_ws(json, position + 1);
        }
        position = json.find(needle, position);
    }
    return std::nullopt;
}

auto extract_json_object_array_field(std::string_view json,
                                     std::string_view field)
    -> util::Result<std::optional<std::vector<std::string>>> {
    const auto start = find_json_value_start(json, field);
    if (!start.has_value()) {
        return std::optional<std::vector<std::string>>{};
    }
    if (*start >= json.size() || json[*start] != '[') {
        return std::unexpected(
            util::make_error("Invalid sparse index dependency metadata."));
    }

    std::vector<std::string> objects;
    std::size_t index = *start + 1;
    while (index < json.size()) {
        index = skip_json_ws(json, index);
        if (index < json.size() && json[index] == ']') {
            return objects;
        }
        if (index >= json.size() || json[index] != '{') {
            return std::unexpected(
                util::make_error("Invalid sparse index dependency metadata."));
        }

        const auto object_start = index;
        bool in_string = false;
        bool escaping = false;
        int depth = 0;
        for (; index < json.size(); ++index) {
            const auto ch = json[index];
            if (escaping) {
                escaping = false;
                continue;
            }
            if (ch == '\\' && in_string) {
                escaping = true;
                continue;
            }
            if (ch == '"') {
                in_string = !in_string;
                continue;
            }
            if (in_string) {
                continue;
            }
            if (ch == '{') {
                ++depth;
            } else if (ch == '}') {
                --depth;
                if (depth == 0) {
                    ++index;
                    objects.emplace_back(json.substr(object_start,
                                                     index - object_start));
                    break;
                }
            }
        }
        if (index > json.size() || (index == json.size() && depth != 0)) {
            return std::unexpected(
                util::make_error("Invalid sparse index dependency metadata."));
        }
        index = skip_json_ws(json, index);
        if (index < json.size() && json[index] == ',') {
            ++index;
            continue;
        }
        if (index < json.size() && json[index] == ']') {
            return objects;
        }
        return std::unexpected(
            util::make_error("Invalid sparse index dependency metadata."));
    }

    return std::unexpected(
        util::make_error("Invalid sparse index dependency metadata."));
}

auto parse_index_dependency(std::string_view object)
    -> util::Result<IndexDependency> {
    const auto name = extract_json_string_field(object, "name");
    if (!name.has_value() || name->empty()) {
        return std::unexpected(
            util::make_error("Invalid sparse index dependency metadata."));
    }

    IndexDependency dependency{};
    dependency.name = *name;
    const auto source =
        extract_json_string_field(object, "source").value_or("registry");
    if (source != "registry") {
        return std::unexpected(util::make_error(std::format(
            "Registry package '{}' declares unsupported '{}' dependency metadata.",
            dependency.name, source)));
    }

    dependency.spec.source = core::DependencySource::Registry;
    dependency.spec.version =
        extract_json_string_field(object, "version").value_or("*");
    if (const auto registry = extract_json_string_field(object, "registry")) {
        dependency.spec.registry = *registry;
    }
    if (const auto features = extract_json_string_array_field(object, "features")) {
        dependency.spec.features = *features;
    }
    if (const auto optional = extract_json_bool_field(object, "optional")) {
        dependency.spec.optional = *optional;
    }
    if (const auto default_features =
            extract_json_bool_field(object, "default_features")) {
        dependency.spec.default_features = *default_features;
    }
    return dependency;
}

auto parse_index_dependencies(std::string_view line,
                              std::span<const std::string> legacy_dependencies)
    -> util::Result<std::vector<IndexDependency>> {
    const auto objects = GUARD(extract_json_object_array_field(line, "deps_v2"));
    std::vector<IndexDependency> dependencies;
    if (objects.has_value()) {
        dependencies.reserve(objects->size());
        for (const auto& object : *objects) {
            dependencies.push_back(GUARD(parse_index_dependency(object)));
        }
        return dependencies;
    }

    dependencies.reserve(legacy_dependencies.size());
    for (const auto& name : legacy_dependencies) {
        IndexDependency dependency{};
        dependency.name = name;
        dependency.spec.source = core::DependencySource::Registry;
        dependency.spec.version = "*";
        dependencies.push_back(std::move(dependency));
    }
    return dependencies;
}

auto parse_index_record(std::string_view line) -> util::Result<IndexRecord> {
    IndexRecord record{};
    if (const auto version = extract_json_string_field(line, "vers")) {
        record.version = *version;
    }
    if (const auto checksum = extract_json_string_field(line, "cksum")) {
        record.checksum = *checksum;
    }
    if (const auto artifact = extract_json_string_field(line, "artifact")) {
        record.artifact = *artifact;
    }
    if (const auto yanked = extract_json_bool_field(line, "yanked")) {
        record.yanked = *yanked;
    }
    if (const auto dependencies = extract_json_string_array_field(line, "deps")) {
        record.dependencies = *dependencies;
    }
    if (const auto owners = extract_json_string_array_field(line, "owners")) {
        record.owners = *owners;
    }
    record.dependency_specs =
        GUARD(parse_index_dependencies(line, record.dependencies));
    if (!record.dependency_specs.empty()) {
        record.dependencies.clear();
        record.dependencies.reserve(record.dependency_specs.size());
        for (const auto& dependency : record.dependency_specs) {
            record.dependencies.push_back(dependency.name);
        }
    }

    if (record.version.empty() || record.checksum.empty()) {
        return std::unexpected(util::make_error("Invalid sparse index row returned by registry."));
    }
    return record;
}

}  // namespace

auto read_index(const core::Manifest& manifest, std::string_view  registry_name,
                std::string_view  package_name)
    -> util::Result<std::vector<IndexRecord>> {
    const auto endpoints = GUARD(resolve_registry_endpoints(manifest, registry_name));
    const auto sparse_path = GUARD(sparse_path_for(package_name));
    const auto index_url = join_url(endpoints.index, sparse_path);
    auto response = GUARD(http_request("GET", index_url));

    if (response.status_code == 404) {
        GUARD(cache_index_body(endpoints.name, package_name, std::string{}));
        return std::vector<IndexRecord>{};
    }
    if (response.status_code < 200 || response.status_code >= 300) {
        return std::unexpected(util::make_error(std::format(
            "Failed to fetch sparse index for '{}' from registry '{}': {}",
            package_name, endpoints.name,
            http_error_message(response, "request failed"))));
    }

    GUARD(cache_index_body(endpoints.name, package_name, response.body));

    std::vector<IndexRecord> records;
    std::stringstream stream(response.body);
    std::string line;
    while (std::getline(stream, line)) {
        line = util::text::trim_ascii_copy(line);
        if (line.empty()) {
            continue;
        }
        auto record = GUARD(parse_index_record(line));
        records.push_back(std::move(record));
    }
    return records;
}

auto semver_greater(std::string_view  lhs, std::string_view  rhs) -> bool {
    auto left = parse_semver(lhs);
    auto right = parse_semver(rhs);
    if (!left || !right) {
        return lhs.compare(rhs) > 0;
    }

    if (std::get<0>(*left) != std::get<0>(*right)) {
        return std::get<0>(*left) > std::get<0>(*right);
    }
    if (std::get<1>(*left) != std::get<1>(*right)) {
        return std::get<1>(*left) > std::get<1>(*right);
    }
    if (std::get<2>(*left) != std::get<2>(*right)) {
        return std::get<2>(*left) > std::get<2>(*right);
    }
    return std::get<3>(*left).compare(std::get<3>(*right)) > 0;
}

auto matches_requirement(std::string_view  version, std::string_view  requirement)
    -> bool {
    if (requirement.empty() || requirement == "*") {
        return true;
    }

    if (requirement.front() == '^') {
        const auto req = requirement.substr(1);
        auto req_semver = parse_semver(req);
        auto ver_semver = parse_semver(version);
        if (!req_semver || !ver_semver) {
            return false;
        }

        if (semver_greater(req, version)) {
            return false;
        }

        const auto req_major = std::get<0>(*req_semver);
        const auto req_minor = std::get<1>(*req_semver);
        const auto req_patch = std::get<2>(*req_semver);
        const auto ver_major = std::get<0>(*ver_semver);
        const auto ver_minor = std::get<1>(*ver_semver);
        const auto ver_patch = std::get<2>(*ver_semver);

        if (req_major > 0) {
            return ver_major == req_major;
        }
        if (req_minor > 0) {
            return ver_major == 0 && ver_minor == req_minor;
        }
        return ver_major == 0 && ver_minor == 0 && ver_patch == req_patch;
    }

    if (version == requirement || version.rfind(std::string(requirement) + "#", 0) == 0) {
        return true;
    }

    if (std::count(requirement.begin(), requirement.end(), '.') < 2) {
        return version.rfind(std::string(requirement) + ".", 0) == 0;
    }

    return false;
}

auto checksum_sha256(const std::filesystem::path& file) -> util::Result<std::string> {
#ifdef _WIN32
    util::process::RunOptions options{};
    options.capture_output = true;
    options.merge_stderr = true;
    options.stdin_mode = util::process::StdinMode::Null;
    const std::vector<std::string> certutil_args{"-hashfile", file.string(), "SHA256"};
    auto run = GUARD(util::process::run_result("certutil", certutil_args, options));
    if (run.exit_code != 0) {
        return std::unexpected(util::make_error("certutil failed to compute SHA256."));
    }

    std::stringstream ss(run.output);
    std::string line;
    static const std::regex hex_line(R"(^[0-9A-Fa-f ]{32,}$)");
    while (std::getline(ss, line)) {
        line = util::text::trim_ascii_copy(line);
        if (!std::regex_match(line, hex_line)) {
            continue;
        }

        std::string digest;
        for (const auto ch : line) {
            if (!std::isspace(static_cast<unsigned char>(ch))) {
                digest.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch))));
            }
        }
        if (!digest.empty()) {
            return digest;
        }
    }

    return std::unexpected(util::make_error("Failed to parse SHA256 from certutil output."));
#else
    util::process::RunOptions options{};
    options.capture_output = true;
    options.stdin_mode = util::process::StdinMode::Null;
    const std::vector<std::string> sha_args{file.string()};
    auto run = GUARD(util::process::run_result("sha256sum", sha_args, options));
    if (run.exit_code != 0) {
        return std::unexpected(util::make_error("sha256sum failed."));
    }
    std::stringstream ss(run.output);
    std::string digest;
    ss >> digest;
    if (digest.empty()) {
        return std::unexpected(util::make_error("Failed to parse sha256sum output."));
    }
    return digest;
#endif
}

auto make_artifact_path(std::string_view  registry, std::string_view  package_name,
                        std::string_view  version)
    -> util::Result<std::filesystem::path> {
    const auto cache = GUARD(package::home::registry_cache_dir());
    const auto dir = cache / registry;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return std::unexpected(util::make_error("Failed to create registry cache dir: " +
                                   dir.string() + " (" + ec.message() + ")"));
    }
    return dir / (std::string(package_name) + "-" + std::string(version) + ".crate");
}

}  // namespace registry::reference::detail





