#include "build/manager_link.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <span>

#include "build/action_graph.hpp"
#include "build/action_scheduler.hpp"
#include "build/link.hpp"
#include "build/settings.hpp"
#include "util/output.hpp"

namespace build::detail {

namespace {

auto read_text_file(const std::filesystem::path& path) -> std::optional<std::string> {
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

auto resolve_library_artifact(std::span<const std::filesystem::path>  library_paths,
                              std::string_view  library)
    -> std::optional<std::filesystem::path> {
    for (const auto& base : library_paths) {
#ifdef _WIN32
        const std::vector<std::filesystem::path> candidates = {
            base / (std::string(library) + ".lib"), base / ("lib" + std::string(library) + ".lib")};
#else
        const std::vector<std::filesystem::path> candidates = {
            base / ("lib" + std::string(library) + ".a"), base / ("lib" + std::string(library) + ".so")};
#endif
        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && !ec) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

auto make_link_progress_observer(const build::actions::ActionGraph& graph)
    -> build::scheduler_runtime::ActionObserver {
    if (!util::output::progress_supported()) {
        return {};
    }

    const auto total = static_cast<std::size_t>(std::count_if(
        graph.nodes.begin(), graph.nodes.end(), [](const build::actions::ActionNode& node) {
            return node.kind == build::actions::ActionKind::Link;
        }));
    if (total == 0) {
        return {};
    }

    return [completed = std::size_t{0}, total](
               const build::scheduler_runtime::ActionEvent& event) mutable {
        if (event.node == nullptr ||
            event.node->kind != build::actions::ActionKind::Link) {
            return;
        }

        if (event.kind == build::scheduler_runtime::ActionEventKind::Started) {
            util::output::progress_begin(util::output::ProgressState{
                .phase = "Linking",
                .completed = completed,
                .total = total,
                .current = event.node->display_name,
            });
            return;
        }

        ++completed;
        util::output::progress_update(util::output::ProgressState{
            .phase = "Linking",
            .completed = completed,
            .total = total,
            .current = event.node->display_name,
        });
        util::output::progress_finish();
    };
}

}  // namespace

auto make_link_signature(const compile::CompilerConfig& config,
                         const std::filesystem::path& output,
                         std::span<const std::filesystem::path>  library_paths,
                         std::span<const std::string>  libraries, bool release)
    -> std::string {
    std::ostringstream out;
    out << config.compiler.generic_string() << "\n";
    out << "profile=" << (release ? "release" : "debug") << "\n";
    out << "output=" << output.generic_string() << "\n";
    out << "flags=\n";
    for (const auto& flag : config.flags) {
        out << flag << "\n";
    }
    out << "linker_flags=\n";
    out << "-fuse-ld=lld\n";
#ifdef _WIN32
    out << "/defaultlib:msvcrt\n";
    out << "/nodefaultlib:libcmt\n";
    out << "/nodefaultlib:libcmtd\n";
    out << "/subsystem:console\n";
#else
    if (release) {
        out << "-s\n";
    }
#endif
    out << "library_paths=\n";
    for (const auto& path : library_paths) {
        out << path.generic_string() << "\n";
    }
    out << "libraries=\n";
    for (const auto& library : libraries) {
        out << library << "\n";
    }
    return out.str();
}

auto needs_link(std::span<const std::filesystem::path>  objects,
                const std::filesystem::path& output, std::size_t compiled_count,
                std::span<const std::filesystem::path>  library_paths,
                std::span<const std::string>  libraries,
                std::string_view  link_signature,
                const std::filesystem::path& link_signature_file) -> bool {
    std::error_code ec;
    if (compiled_count > 0 || !std::filesystem::exists(output, ec) || ec) {
        return true;
    }

    const auto output_time = std::filesystem::last_write_time(output, ec);
    if (ec) {
        return true;
    }

    for (const auto& object : objects) {
        if (!std::filesystem::exists(object, ec) || ec) {
            return true;
        }
        const auto object_time = std::filesystem::last_write_time(object, ec);
        if (ec || object_time > output_time) {
            return true;
        }
    }

    for (const auto& library : libraries) {
        const auto resolved = resolve_library_artifact(library_paths, library);
        if (!resolved.has_value()) {
            return true;
        }
        const auto lib_time = std::filesystem::last_write_time(*resolved, ec);
        if (ec || lib_time > output_time) {
            return true;
        }
    }

    if (!std::filesystem::exists(link_signature_file, ec) || ec) {
        return true;
    }
    const auto stored_signature = read_text_file(link_signature_file);
    if (!stored_signature.has_value()) {
        return true;
    }
    if (*stored_signature != link_signature) {
        return true;
    }

    return false;
}

auto execute_link_pipeline(
    const compile::CompilerConfig& config, const std::filesystem::path& build_root,
    std::span<const std::filesystem::path>  objects,
    const std::filesystem::path& output,
    std::span<const std::filesystem::path>  library_paths,
    std::span<const std::string>  libraries,
    std::span<const std::filesystem::path> runtime_files,
    const std::filesystem::path& link_signature_file,
    std::string_view  link_signature, bool release) -> LinkPipelineResult {
    build::actions::ActionGraph graph;
    const auto link_action = build::actions::append_action(
        graph, build::actions::ActionKind::Link, build::actions::PoolKind::Link, {},
        build::actions::LinkActionPayload{
            .objects = std::vector<std::filesystem::path>(objects.begin(), objects.end()),
            .output = output,
            .library_paths = std::vector<std::filesystem::path>(library_paths.begin(), library_paths.end()),
            .libraries = std::vector<std::string>(libraries.begin(), libraries.end()),
            .link_signature_file = link_signature_file,
            .link_signature = std::string(link_signature),
            .release = release,
        },
        1000.0, 768, output.filename().generic_string());
    build::actions::append_action(
        graph, build::actions::ActionKind::CopyRuntime,
        build::actions::PoolKind::Link, {link_action},
        build::actions::CopyRuntimePayload{
            .build_root = build_root,
            .runtime_files =
                std::vector<std::filesystem::path>(runtime_files.begin(),
                                                   runtime_files.end()),
        },
        50.0, 64, "copy-runtime");

    const auto link_actions = static_cast<std::size_t>(std::count_if(
        graph.nodes.begin(), graph.nodes.end(), [](const build::actions::ActionNode& node) {
            return node.kind == build::actions::ActionKind::Link;
        }));
    const auto link_concurrency =
        build::settings::resolve_link_concurrency(link_actions);

    build::scheduler_runtime::CapacityPlan plan{};
    plan.total_slots = link_concurrency.jobs;
    plan.reserve_mb = 1536;
    plan.memory_budget_mb =
        plan.reserve_mb + static_cast<std::uint64_t>(plan.total_slots) * 768;
    plan.reason = "post-compile link pipeline (" + link_concurrency.reason + ")";
    plan.pools = {
        {.kind = build::actions::PoolKind::Compile, .depth = 1},
        {.kind = build::actions::PoolKind::Check, .depth = 1},
        {.kind = build::actions::PoolKind::Link, .depth = plan.total_slots},
        {.kind = build::actions::PoolKind::Console, .depth = 1},
    };

    build::scheduler_runtime::SchedulerOptions options{};
    options.observer = make_link_progress_observer(graph);
    std::optional<LinkFailureInfo> failure;
    const auto scheduler_result = build::scheduler_runtime::execute_action_graph(
        graph, plan, options,
        [&config, &failure](const build::actions::ActionNode& node,
                            const std::atomic_bool* cancel_requested)
            -> util::Status {
            if (std::holds_alternative<build::actions::LinkActionPayload>(
                    node.payload)) {
                const auto& payload =
                    std::get<build::actions::LinkActionPayload>(node.payload);
                auto result = GUARD(build::link::link_binary_result(
                    config, payload.objects, payload.output, payload.library_paths,
                    payload.libraries, payload.release, cancel_requested));
                if (result.canceled) {
                    return util::Ok;
                }
                if (result.exit_code != 0) {
                    failure = LinkFailureInfo{
                        .exit_code = result.exit_code,
                        .output = std::move(result.output),
                    };
                    return std::unexpected(util::make_error(
                        "Linking failed for output: " + payload.output.string()));
                }
                return util::Ok;
            }

            const auto& payload =
                std::get<build::actions::CopyRuntimePayload>(node.payload);
            return build::link::copy_runtime_dlls(payload.runtime_files,
                                                  payload.build_root);
        });

    if (!scheduler_result.success) {
        return LinkPipelineResult{
            .success = false,
            .error = scheduler_result.first_error.value_or(
                util::make_error("Link pipeline failed.")),
            .failure = std::move(failure),
        };
    }
    return LinkPipelineResult{
        .success = true,
        .error = std::nullopt,
        .failure = std::nullopt,
    };
}

}  // namespace build::detail











