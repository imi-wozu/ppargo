#include "build/manager.hpp"

#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <vector>

#include "build/compile.hpp"
#include "build/link.hpp"
#include "build/manager_link.hpp"
#include "build/manager_sources.hpp"
#include "build/targets.hpp"
#include "core/paths.hpp"
#include "util/fs.hpp"

namespace build {

namespace {

auto variant_object_root(const std::filesystem::path& build_root,
                         const compile::CompilerConfig& config)
    -> std::filesystem::path {
    const auto signature = compile::compile_signature(config);
    const auto signature_hash =
        std::to_string(std::hash<std::string_view>{}(signature));
    return build_root / "obj" / signature_hash;
}

auto link_signature_file_for(const std::filesystem::path& build_root,
                             const std::filesystem::path& output)
    -> std::filesystem::path {
    const auto hash =
        std::to_string(std::hash<std::string>{}(output.generic_string()));
    return build_root / (".link_signature_" + hash);
}

}  // namespace

auto execute(const std::filesystem::path& root, const core::Manifest& manifest,
             const BuildOptions& options)
    -> util::Result<BuildExecutionResult> {
    BuildExecutionResult result{};
    const auto resolved_target =
        GUARD(targets::resolve_build_target(root, manifest, options.target));
    result.target_name = resolved_target.name;

    const auto selected_kind = options.target.has_value()
                                   ? options.target->kind
                                   : targets::SelectionKind::DefaultBin;
    const bool target_is_test =
        selected_kind == targets::SelectionKind::UnitTest ||
        selected_kind == targets::SelectionKind::IntegrationTest;

    const std::filesystem::path build_root =
        core::build_dir(root, manifest, options.release,
                        options.output_dir_override);
    result.output_binary = build_root / resolved_target.binary_name;

    std::error_code ec;
    std::filesystem::create_directories(build_root, ec);
    if (ec) {
        return std::unexpected(util::make_error(
            "Failed to create build directory: " + build_root.string() + " (" +
            ec.message() + ")"));
    }

    auto collected_sources = GUARD(detail::collect_target_sources(
        root, manifest, resolved_target, selected_kind,
        options.output_dir_override));

    const auto compile_mode = options.mode == BuildOptions::Mode::Check
                                  ? compile::CompileMode::Check
                                  : compile::CompileMode::Build;
    auto config = GUARD(compile::make_compiler_config(
        root, manifest, options.release, target_is_test, compile_mode,
        collected_sources.sources.size(), options.output_dir_override,
        options.dependency_artifacts.include_directories));

    if (options.mode == BuildOptions::Mode::Check) {
        GUARD(compile::run_checks_with_cache(
            root, build_root, collected_sources.sources, config));
        return result;
    }

    const std::filesystem::path obj_root = variant_object_root(build_root, config);
    std::filesystem::create_directories(obj_root, ec);
    if (ec) {
        return std::unexpected(util::make_error(
            "Failed to create object directory: " + obj_root.string() + " (" +
            ec.message() + ")"));
    }

    auto compile_result = GUARD(compile::compile_objects(
        root, obj_root, collected_sources.sources, config, false));
    result.compiled_units = compile_result.compiled_count;

    const std::filesystem::path output = result.output_binary;
    const std::filesystem::path link_signature_file =
        link_signature_file_for(build_root, output);
    const auto link_signature = detail::make_link_signature(
        config, output, options.dependency_artifacts.library_directories,
        options.dependency_artifacts.link_libraries, options.release);
    GUARD(link::ensure_not_running_target(output));

    if (detail::needs_link(compile_result.objects, output,
                           compile_result.compiled_count,
                           options.dependency_artifacts.library_directories,
                           options.dependency_artifacts.link_libraries,
                           link_signature,
                           link_signature_file)) {
        auto first_link = detail::execute_link_pipeline(
            config, build_root, compile_result.objects, output,
            options.dependency_artifacts.library_directories,
            options.dependency_artifacts.link_libraries,
            options.dependency_artifacts.runtime_files, link_signature_file,
            link_signature, options.release);
        if (first_link.success) {
            result.linked = true;
        } else {
            return std::unexpected(first_link.error.value_or(
                util::make_error("Link pipeline failed.")));
        }
    }

    if (result.linked) {
        GUARD(util::fs::atomic_write_text_result(link_signature_file,
                                                 link_signature));
    }

    return result;
}

}  // namespace build
