#include <chrono>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

#include "build/manager.hpp"
#include "cli/commands/commands.hpp"
#include "cli/commands/common.hpp"
#include "package/manager.hpp"
#include "util/output.hpp"

namespace cli {

namespace {

auto make_selection_options(const BuildCommand& command)
    -> build::targets::BuildSelectionOptions {
    return build::targets::BuildSelectionOptions{
        .bins = command.bins,
        .bins_all = command.bins_all,
        .examples = command.examples,
        .examples_all = command.examples_all,
        .tests = command.tests,
        .tests_all = command.tests_all,
        .benches = {},
        .benches_all = false,
    };
}

auto make_package_selection_options(const BuildCommand& command)
    -> commands::common::PackageSelectionOptions {
    return commands::common::PackageSelectionOptions{
        .workspace = command.workspace,
        .all = command.all_packages,
        .packages = command.packages,
        .excludes = command.exclude_packages,
    };
}

}  // namespace

auto BuildCommand::execute() const -> util::Status {
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

    std::size_t built_targets = 0;
    for (auto& context : contexts) {
        const auto selections = GUARD(build::targets::resolve_build_selections(
            context.project.project.root, context.project.project.manifest,
            make_selection_options(*this)));
        commands::common::log_selected_targets(context.project, verbose,
                                               "building", selections);

        for (const auto& selection : selections) {
            auto dependency_artifacts =
                GUARD(commands::common::dependency_artifacts_for_selection(
                    context, selection));
            build::BuildOptions options{
                .release = release,
                .mode = build::BuildOptions::Mode::Build,
                .target = selection,
                .output_dir_override = context.project.output_dir_override,
                .dependency_artifacts = std::move(dependency_artifacts),
            };
            GUARD(build::execute(context.project.project.root,
                                 context.project.project.manifest, options));
            ++built_targets;
        }
    }

    const auto elapsed{commands::common::elapsed_seconds(start)};

    util::output::argo_status(
        "Finished", util::output::Color::Green,
        std::format("`{}` profile {} {} target(s) in {:.2f}s",
                    commands::common::profile_name(release),
                    commands::common::profile_descriptor(release),
                    built_targets, elapsed));
    return util::Ok;
}

}  // namespace cli
