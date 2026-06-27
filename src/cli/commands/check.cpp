#include "cli/commands/commands.hpp"

#include <chrono>
#include <filesystem>
#include <format>
#include <string_view>
#include <vector>
#include <utility>

#include "build/manager.hpp"
#include "build/targets.hpp"
#include "cli/commands/common.hpp"
#include "package/manager.hpp"
#include "util/output.hpp"

namespace cli {

namespace {

auto make_selection_options(const CheckCommand& command)
    -> build::targets::BuildSelectionOptions {
    return build::targets::BuildSelectionOptions{
        .bins = command.bins,
        .bins_all = command.bins_all,
        .examples = command.examples,
        .examples_all = command.examples_all,
        .tests = command.tests,
        .tests_all = command.tests_all,
        .benches = command.benches,
        .benches_all = command.benches_all,
    };
}

auto make_package_selection_options(const CheckCommand& command)
    -> commands::common::PackageSelectionOptions {
    return commands::common::PackageSelectionOptions{
        .workspace = command.workspace,
        .all = command.all_packages,
        .packages = command.packages,
        .excludes = command.exclude_packages,
    };
}

}  // namespace

auto CheckCommand::execute() const -> util::Status {
    const auto start{std::chrono::steady_clock::now()};
    commands::common::ScopedCompileEnvironment scoped_environment(
        commands::common::compile_environment_settings(jobs, verbose));

    auto contexts = GUARD(commands::common::prepare_build_command_contexts(
        manifest_path, target_dir,
        commands::common::make_dependency_workflow_options(locked, offline,
                                                           frozen, features,
                                                           all_features,
                                                           no_default_features),
        make_package_selection_options(*this)));

    std::size_t checked_targets = 0;
    std::vector<std::string> failed_targets;
    for (auto& context : contexts) {
        const auto selections = GUARD(build::targets::resolve_build_selections(
            context.project.project.root, context.project.project.manifest,
            make_selection_options(*this)));
        commands::common::log_selected_targets(context.project, verbose,
                                               "checking", selections);

        for (const auto& selection : selections) {
            auto dependency_artifacts =
                GUARD(commands::common::dependency_artifacts_for_selection(
                    context, selection));
            build::BuildOptions options{
                .release = release,
                .mode = build::BuildOptions::Mode::Check,
                .target = selection,
                .output_dir_override = context.project.output_dir_override,
                .dependency_artifacts = std::move(dependency_artifacts),
            };
            auto result =
                build::execute(context.project.project.root,
                               context.project.project.manifest, options);
            ++checked_targets;
            if (result.has_value()) {
                continue;
            }
            if (!keep_going) {
                return std::unexpected(std::move(result.error()));
            }
            failed_targets.push_back(std::format(
                "{}:{}",
                context.project.project.manifest.package.name,
                commands::common::selection_label(selection)));
        }
    }

    if (!failed_targets.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < failed_targets.size(); ++i) {
            if (i != 0) {
                joined += ", ";
            }
            joined += failed_targets[i];
        }
        return util::make_unexpected(std::format(
            "Check Error: {} target(s) failed: {}.", failed_targets.size(),
            joined));
    }

    util::output::argo_status(
        "Finished", util::output::Color::Green,
        std::format("`{}` profile {} {} target(s) in {:.2f}s",
                    commands::common::profile_name(release),
                    commands::common::profile_descriptor(release),
                    checked_targets,
                    commands::common::elapsed_seconds(start)));
    return util::Ok;
}

}  // namespace cli
