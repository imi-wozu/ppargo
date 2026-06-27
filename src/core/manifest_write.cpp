#include "core/manifest.hpp"

#include <format>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <span>

#include "util/fs.hpp"

namespace {

auto toml_quote(std::string value) -> std::string {
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

auto path_as_toml_string(const std::filesystem::path& path) -> std::string {
    return path.generic_string();
}

auto array_to_toml(std::span<const std::string>  values) -> std::string {
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

auto dependency_is_simple_version(const core::DependencySpec& spec) -> bool {
    return spec.source == core::DependencySource::Registry && !spec.version.empty() &&
           spec.path.empty() && spec.git.empty() && spec.rev.empty() &&
           spec.registry.empty() && spec.features.empty() && !spec.optional &&
           spec.default_features;
}

auto dependency_to_toml(const core::DependencySpec& spec) -> std::string {
    if (dependency_is_simple_version(spec)) {
        return toml_quote(spec.version);
    }

    std::vector<std::string> parts;
    if (!spec.version.empty()) {
        parts.push_back("version = " + toml_quote(spec.version));
    }
    if (!spec.path.empty()) {
        parts.push_back("path = " + toml_quote(path_as_toml_string(spec.path)));
    }
    if (!spec.git.empty()) {
        parts.push_back("git = " + toml_quote(spec.git));
    }
    if (!spec.rev.empty()) {
        parts.push_back("rev = " + toml_quote(spec.rev));
    }
    if (!spec.registry.empty()) {
        parts.push_back("registry = " + toml_quote(spec.registry));
    }
    if (!spec.features.empty()) {
        parts.push_back("features = " + array_to_toml(spec.features));
    }
    if (spec.optional) {
        parts.push_back("optional = true");
    }
    if (!spec.default_features) {
        parts.push_back("default_features = false");
    }

    std::ostringstream out;
    out << "{ ";
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << parts[i];
    }
    out << " }";
    return out.str();
}

auto emit_dependency_section(std::ostringstream& out,
                             std::string_view  section_name,
                             const core::DependencyMap& deps) -> void {
    out << "[" << section_name << "]\n";
    for (const auto& [name, spec] : deps) {
        out << name << " = " << dependency_to_toml(spec) << "\n";
    }
    out << "\n";
}

auto path_array_to_toml(std::span<const std::filesystem::path> values)
    -> std::string {
    std::vector<std::string> strings;
    strings.reserve(values.size());
    for (const auto& value : values) {
        strings.push_back(path_as_toml_string(value));
    }
    return array_to_toml(strings);
}

}  // namespace

namespace core {

auto save_manifest(const std::filesystem::path& path, const Manifest& manifest)
    -> util::Status {
    auto toml = to_toml(manifest);
    if (!toml) {
        return std::unexpected(toml.error());
    }
    return util::fs::atomic_write_text_result(path, *toml);
}

auto to_toml(const Manifest& manifest) -> util::Result<std::string> {
    std::ostringstream out;
    const auto defaults = default_manifest(manifest.package.name);
    const bool has_package_manager = !manifest.features.package_manager.empty();

    if (manifest.features.packages && !has_package_manager) {
        return std::unexpected(util::make_error(
            "Cannot save manifest: [features].packages=true requires a non-empty [features].package_manager."));
    }

    if (manifest.package_defined) {
        out << "[package]\n";
        out << "name = " << toml_quote(manifest.package.name) << "\n";
        out << "version = " << toml_quote(manifest.package.version) << "\n";
        out << "edition = " << toml_quote(manifest.package.edition) << "\n\n";
    }

    if (manifest.workspace.enabled) {
        out << "[workspace]\n";
        out << "members = " << path_array_to_toml(manifest.workspace.members)
            << "\n";
        if (!manifest.workspace.exclude.empty()) {
            out << "exclude = "
                << path_array_to_toml(manifest.workspace.exclude) << "\n";
        }
        out << "\n";
    }

    if (!manifest.package_defined && manifest.workspace.enabled) {
        return out.str();
    }

    out << "[toolchain]\n";
    out << "compiler = " << toml_quote(manifest.toolchain.compiler) << "\n";
    out << "\n";

    emit_dependency_section(out, "dependencies", manifest.dependencies);
    if (!manifest.dev_dependencies.empty()) {
        emit_dependency_section(out, "dev-dependencies", manifest.dev_dependencies);
    }
    if (!manifest.build_dependencies.empty()) {
        emit_dependency_section(out, "build-dependencies", manifest.build_dependencies);
    }

    const bool emit_package_manager =
        has_package_manager &&
        (manifest.features.packages ||
         manifest.features.package_manager != defaults.features.package_manager);
    const bool packages_implied = has_package_manager;
    const bool emit_vcpkg_root =
        has_package_manager && manifest.features.package_manager == "vcpkg" &&
        !manifest.features.vcpkg_root.empty() &&
        manifest.features.vcpkg_root != defaults.features.vcpkg_root;
    const bool emit_packages = manifest.features.packages != packages_implied;
    const bool emit_features =
        emit_packages || emit_package_manager || emit_vcpkg_root ||
        !manifest.features.package_features.empty();

    if (emit_features) {
        out << "[features]\n";
        if (emit_packages) {
            out << "packages = "
                << (manifest.features.packages ? "true" : "false") << "\n";
        }
        if (emit_package_manager) {
            out << "package_manager = "
                << toml_quote(manifest.features.package_manager) << "\n";
        }
        if (emit_vcpkg_root) {
            out << "vcpkg_root = "
                << toml_quote(path_as_toml_string(manifest.features.vcpkg_root))
                << "\n";
        }
        for (const auto& [name, values] : manifest.features.package_features) {
            out << name << " = " << array_to_toml(values) << "\n";
        }
        out << "\n";
    }

    const bool emit_build =
        manifest.build.source_dir != defaults.build.source_dir ||
        manifest.build.include_dirs != defaults.build.include_dirs ||
        manifest.build.exclude != defaults.build.exclude ||
        manifest.build.output_dir != defaults.build.output_dir ||
        manifest.build.binary_name != defaults.build.binary_name ||
        manifest.build.modules != defaults.build.modules ||
        manifest.build.module_interface_exts != defaults.build.module_interface_exts ||
        manifest.build.module_output_dir != defaults.build.module_output_dir ||
        manifest.build.aggressive_tu_threshold !=
            defaults.build.aggressive_tu_threshold ||
        manifest.build.aggressive_stale_threshold !=
            defaults.build.aggressive_stale_threshold ||
        manifest.build.pch_scan_lines != defaults.build.pch_scan_lines ||
        manifest.build.pch_frequency_threshold !=
            defaults.build.pch_frequency_threshold ||
        manifest.build.pch_max_headers != defaults.build.pch_max_headers ||
        manifest.build.depscan_timeout_ms != defaults.build.depscan_timeout_ms;

    if (emit_build) {
        out << "[build]\n";
        out << "source_dir = "
            << toml_quote(path_as_toml_string(manifest.build.source_dir)) << "\n";

        std::vector<std::string> include_dirs;
        include_dirs.reserve(manifest.build.include_dirs.size());
        for (const auto& dir : manifest.build.include_dirs) {
            include_dirs.push_back(path_as_toml_string(dir));
        }
        out << "include_dirs = " << array_to_toml(include_dirs) << "\n";
        out << "exclude = " << array_to_toml(manifest.build.exclude) << "\n";
        out << "output_dir = "
            << toml_quote(path_as_toml_string(manifest.build.output_dir)) << "\n";
        if (!manifest.build.binary_name.empty()) {
            out << "binary_name = " << toml_quote(manifest.build.binary_name)
                << "\n";
        }
        if (manifest.build.modules) {
            out << "modules = true\n";
        }
        if (manifest.build.module_interface_exts !=
            defaults.build.module_interface_exts) {
            out << "module_interface_exts = "
                << array_to_toml(manifest.build.module_interface_exts) << "\n";
        }
        if (manifest.build.module_output_dir != defaults.build.module_output_dir) {
            out << "module_output_dir = "
                << toml_quote(
                       path_as_toml_string(manifest.build.module_output_dir))
                << "\n";
        }
        if (manifest.build.aggressive_tu_threshold !=
            defaults.build.aggressive_tu_threshold) {
            out << "aggressive_tu_threshold = "
                << manifest.build.aggressive_tu_threshold << "\n";
        }
        if (manifest.build.aggressive_stale_threshold !=
            defaults.build.aggressive_stale_threshold) {
            out << "aggressive_stale_threshold = "
                << manifest.build.aggressive_stale_threshold << "\n";
        }
        if (manifest.build.pch_scan_lines != defaults.build.pch_scan_lines) {
            out << "pch_scan_lines = " << manifest.build.pch_scan_lines << "\n";
        }
        if (manifest.build.pch_frequency_threshold !=
            defaults.build.pch_frequency_threshold) {
            out << "pch_frequency_threshold = "
                << std::format("{:.2f}", manifest.build.pch_frequency_threshold)
                << "\n";
        }
        if (manifest.build.pch_max_headers != defaults.build.pch_max_headers) {
            out << "pch_max_headers = " << manifest.build.pch_max_headers << "\n";
        }
        if (manifest.build.depscan_timeout_ms !=
            defaults.build.depscan_timeout_ms) {
            out << "depscan_timeout_ms = "
                << manifest.build.depscan_timeout_ms << "\n";
        }
        out << "\n";
    }

    if (!manifest.registries.empty()) {
        out << "[registries]\n";
        for (const auto& [name, config] : manifest.registries) {
            std::vector<std::string> parts;
            parts.push_back("index = " + toml_quote(config.index));
            if (!config.api.empty()) {
                parts.push_back("api = " + toml_quote(config.api));
            }
            out << name << " = { ";
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << parts[i];
            }
            out << " }\n";
        }
    }

    return out.str();
}

}  // namespace core

