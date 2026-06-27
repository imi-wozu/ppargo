#include "package/manager_mutation.hpp"

#include <fstream>
#include <format>
#include <optional>
#include <regex>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "util/fs.hpp"
#include "util/text.hpp"
#include "package/vcpkg.hpp"
#include "registry/reference.hpp"

namespace package::detail {

namespace {

struct SectionRange {
    std::string name;
    std::size_t start = 0;
    std::size_t end = 0;
};

auto detect_newline(std::string_view content) -> std::string_view {
    return content.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
}

auto normalize_newlines(std::string_view content, std::string_view newline)
    -> std::string {
    if (newline == "\n") {
        return std::string(content);
    }

    std::string normalized;
    normalized.reserve(content.size() + 16);
    for (char ch : content) {
        if (ch == '\n') {
            normalized.append(newline);
            continue;
        }
        normalized.push_back(ch);
    }
    return normalized;
}

auto parse_section_header(std::string_view line) -> std::optional<std::string> {
    auto trimmed = util::text::trim_ascii_copy(line);
    const auto comment = trimmed.find('#');
    if (comment != std::string::npos) {
        trimmed = util::text::trim_ascii_copy(trimmed.substr(0, comment));
    }

    if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') {
        return std::nullopt;
    }

    return util::text::trim_ascii_copy(trimmed.substr(1, trimmed.size() - 2));
}

auto collect_section_ranges(std::string_view content) -> std::vector<SectionRange> {
    std::vector<SectionRange> sections;

    std::size_t pos = 0;
    while (pos < content.size()) {
        const auto line_end = content.find('\n', pos);
        const auto raw_end =
            line_end == std::string_view::npos ? content.size() : line_end;
        auto line = content.substr(pos, raw_end - pos);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        if (const auto header = parse_section_header(line); header.has_value()) {
            if (!sections.empty()) {
                sections.back().end = pos;
            }
            sections.push_back(SectionRange{
                .name = *header,
                .start = pos,
                .end = content.size(),
            });
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        pos = line_end + 1;
    }

    return sections;
}

auto find_section_range(std::span<const SectionRange> sections,
                        std::string_view name) -> const SectionRange* {
    for (const auto& section : sections) {
        if (section.name == name) {
            return &section;
        }
    }
    return nullptr;
}

auto read_text_file(const std::filesystem::path& path) -> util::Result<std::string> {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::unexpected(
            util::make_error("Failed to open manifest: " + path.string()));
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

auto rendered_dependencies_section(const core::Manifest& manifest,
                                   std::string_view newline)
    -> util::Result<std::string> {
    const auto canonical = GUARD(core::to_toml(manifest));
    const auto sections = collect_section_ranges(canonical);
    const auto* dependencies = find_section_range(sections, "dependencies");
    if (dependencies == nullptr) {
        return std::unexpected(util::make_error(
            "Failed to render [dependencies] section from manifest."));
    }

    return normalize_newlines(
        std::string_view(canonical).substr(dependencies->start,
                                           dependencies->end - dependencies->start),
        newline);
}

auto dependencies_insert_position(std::span<const SectionRange> sections,
                                  std::size_t fallback) -> std::size_t {
    for (const auto section_name :
         {"dependencies", "dev-dependencies", "build-dependencies", "features",
          "build", "registries"}) {
        if (const auto* section = find_section_range(sections, section_name);
            section != nullptr) {
            return section->start;
        }
    }

    if (const auto* toolchain = find_section_range(sections, "toolchain");
        toolchain != nullptr) {
        return toolchain->end;
    }

    if (const auto* package = find_section_range(sections, "package");
        package != nullptr) {
        return package->end;
    }

    return fallback;
}

}  // namespace

auto validate_package_name(std::string_view  package_name) -> util::Status {
    static const std::regex pattern{"^[A-Za-z0-9][A-Za-z0-9+_.-]*$"};
    if (!std::regex_match(package_name.begin(), package_name.end(), pattern)) {
        return std::unexpected(util::make_error("Invalid package name. Allowed characters: letters, digits, '+', '_', '-', '.'."));
    }
    return util::Ok;
}

auto parse_dependency_input(std::string_view  dep_spec)
    -> util::Result<ParsedDependencyInput> {
    const auto at{dep_spec.find('@')};
    ParsedDependencyInput parsed{};
    if (at == std::string::npos) {
        parsed.name = dep_spec;
    } else {
        parsed.name = dep_spec.substr(0, at);
        parsed.version = dep_spec.substr(at + 1);
    }

    parsed.name = std::string(parsed.name);
    if (parsed.version.has_value()) {
        *parsed.version = std::string(*parsed.version);
    }

    if (parsed.name.empty()) {
        return std::unexpected(util::make_error("dependency name must not be empty."));
    }
    GUARD(validate_package_name(parsed.name));

    if (parsed.version.has_value() && parsed.version->empty()) {
        return std::unexpected(util::make_error("dependency version after '@' must not be empty."));
    }

    return parsed;
}

auto infer_version_for_add(const std::filesystem::path& project_root,
                           const core::Manifest& manifest,
                           std::string_view  dependency_name)
    -> util::Result<std::string> {
    if (manifest.features.package_manager == "vcpkg") {
        auto matches = GUARD(package::vcpkg::search_packages(manifest, dependency_name));
        if (matches.empty()) {
            return std::unexpected(util::make_error(std::format("Package '{}' not found in the vcpkg registry.",
                            dependency_name)));
        }
        for (const auto& candidate : matches) {
            if (candidate.name == dependency_name ||
                candidate.name.rfind(std::string(dependency_name) + ":", 0) == 0) {
                return candidate.version;
            }
        }
        return matches.front().version;
    }

    if (manifest.features.package_manager == "ppargo") {
        auto resolved = GUARD(registry::reference::resolve_registry_package(
                manifest, "", dependency_name, "*"));
        (void)project_root;
        return resolved.version;
    }

    return std::unexpected(util::make_error("Unsupported package manager: " +
                               manifest.features.package_manager));
}

auto load_project_manifest(const std::filesystem::path& project_root)
    -> util::Result<core::Manifest> {
    return core::load_manifest(project_root / "ppargo.toml");
}

auto save_project_manifest(const std::filesystem::path& project_root,
                           const core::Manifest& manifest) -> util::Status {
    return core::save_manifest(project_root / "ppargo.toml", manifest);
}

auto save_project_dependencies(const std::filesystem::path& project_root,
                               const core::Manifest& manifest) -> util::Status {
    const auto manifest_path = project_root / "ppargo.toml";
    auto existing = GUARD(read_text_file(manifest_path));
    const auto newline = detect_newline(existing);
    auto dependencies = GUARD(rendered_dependencies_section(manifest, newline));

    auto sections = collect_section_ranges(existing);
    if (const auto* current = find_section_range(sections, "dependencies");
        current != nullptr) {
        existing.replace(current->start, current->end - current->start,
                         dependencies);
    } else {
        const auto insert_pos = dependencies_insert_position(sections, existing.size());
        existing.insert(insert_pos, dependencies);
    }

    return util::fs::atomic_write_text_result(manifest_path, existing);
}

}  // namespace package::detail



