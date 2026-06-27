#include "cli/commands/commands.hpp"

#include "build/manager.hpp"
#include "build/targets.hpp"
#include "cli/commands/common.hpp"
#include "package/manager.hpp"
#include "util/output.hpp"
#include "util/process.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <utility>

namespace cli {

namespace {

auto to_target_scope(TestScope scope) -> build::targets::TestScope {
    switch (scope) {
        case TestScope::Unit:
            return build::targets::TestScope::Unit;
        case TestScope::Integration:
            return build::targets::TestScope::Integration;
        case TestScope::All:
        default:
            return build::targets::TestScope::All;
    }
}

auto make_selection_options(const TestCommand& command)
    -> build::targets::TestSelectionOptions {
    return build::targets::TestSelectionOptions{
        .scope = to_target_scope(command.scope),
        .tests = command.tests,
        .tests_all = command.tests_all,
        .examples = command.examples,
        .examples_all = command.examples_all,
        .benches = command.benches,
        .benches_all = command.benches_all,
        .include_default_examples = false,
    };
}

auto make_package_selection_options(const TestCommand& command)
    -> commands::common::PackageSelectionOptions {
    return commands::common::PackageSelectionOptions{
        .workspace = command.workspace,
        .all = command.all_packages,
        .packages = command.packages,
        .excludes = command.exclude_packages,
    };
}

auto selection_priority(const build::targets::Selection& selection) -> int {
    switch (selection.kind) {
        case build::targets::SelectionKind::IntegrationTest:
            return 0;
        case build::targets::SelectionKind::UnitTest:
            return 1;
        case build::targets::SelectionKind::Example:
            return 2;
        case build::targets::SelectionKind::Bench:
            return 3;
        case build::targets::SelectionKind::Bin:
        case build::targets::SelectionKind::DefaultBin:
            return 4;
    }
    return 5;
}

void sort_selections(std::vector<build::targets::Selection>& selections) {
    std::stable_sort(selections.begin(), selections.end(),
                     [](const build::targets::Selection& lhs,
                        const build::targets::Selection& rhs) {
                         const auto lhs_priority = selection_priority(lhs);
                         const auto rhs_priority = selection_priority(rhs);
                         if (lhs_priority != rhs_priority) {
                             return lhs_priority < rhs_priority;
                         }
                         return lhs.name < rhs.name;
                     });
}

auto build_run_args(const TestCommand& command) -> std::vector<std::string> {
    std::vector<std::string> args;
    if (command.test_filter.has_value()) {
        args.push_back(*command.test_filter);
    }
    args.insert(args.end(), command.passthrough_args.begin(),
                command.passthrough_args.end());
    return args;
}

auto is_runnable_selection(const build::targets::Selection& selection,
                           std::span<const build::targets::Selection> run_targets)
    -> bool {
    return std::any_of(run_targets.begin(), run_targets.end(),
                       [&](const build::targets::Selection& candidate) {
                           return candidate.kind == selection.kind &&
                                  candidate.name == selection.name;
                       });
}

}  // namespace

auto TestCommand::execute() const -> util::Status {
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

    struct PlannedContext {
        commands::common::PreparedBuildCommandContext context;
        build::targets::ResolvedTestPlan plan;
    };
    std::vector<PlannedContext> planned_contexts;
    planned_contexts.reserve(contexts.size());
    std::size_t total_tests = 0;
    for (auto& context : contexts) {
        auto plan = GUARD(build::targets::resolve_test_plan(
            context.project.project.root, context.project.project.manifest,
            make_selection_options(*this)));
        sort_selections(plan.build_targets);
        sort_selections(plan.run_targets);
        total_tests += plan.run_targets.size();
        commands::common::log_selected_targets(context.project, verbose,
                                               "testing", plan.build_targets);
        planned_contexts.push_back(
            PlannedContext{.context = std::move(context), .plan = std::move(plan)});
    }

    struct BuiltTarget {
        build::targets::Selection selection;
        build::BuildExecutionResult result;
    };

    std::size_t built_count = 0;

    const auto run_args = build_run_args(*this);
    const bool show_progress = util::output::progress_supported();
    std::vector<std::string> failed_targets;
    std::size_t executed_tests = 0;

    for (auto& planned : planned_contexts) {
        auto& context = planned.context;
        auto& plan = planned.plan;
        std::vector<BuiltTarget> built_targets;
        built_targets.reserve(plan.build_targets.size());
        for (const auto& selection : plan.build_targets) {
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
            built_targets.push_back(BuiltTarget{
                .selection = selection,
                .result = GUARD(build::execute(context.project.project.root,
                                               context.project.project.manifest,
                                               options)),
            });
            ++built_count;
        }

        if (no_run) {
            continue;
        }

        for (const auto& built_target : built_targets) {
            if (!is_runnable_selection(built_target.selection, plan.run_targets)) {
                continue;
            }

            std::error_code ec;
            if (!std::filesystem::exists(built_target.result.output_binary, ec) ||
                ec) {
                return std::unexpected(util::make_error(std::format(
                    "Test Error: Built test executable not found: {}",
                    built_target.result.output_binary.string())));
            }

            const auto relative_exe = std::filesystem::relative(
                built_target.result.output_binary, context.project.project.root,
                ec);
            const auto display_path =
                ec ? built_target.result.output_binary.generic_string()
                   : relative_exe.generic_string();

            if (show_progress) {
                util::output::progress_begin(util::output::ProgressState{
                    .phase = "Running",
                    .completed = executed_tests,
                    .total = total_tests,
                    .current = display_path,
                });
                util::output::progress_finish();
            } else {
                util::output::argo_status("Running", util::output::Color::Green,
                                          std::format("`{}`", display_path));
            }

            util::process::RunOptions run_options{};
            run_options.working_directory = context.project.project.root;
            auto run_result = GUARD(util::process::run_result(
                built_target.result.output_binary, run_args, run_options));
            ++executed_tests;
            if (run_result.exit_code != 0) {
                failed_targets.push_back(std::format(
                    "{}:{}", context.project.project.manifest.package.name,
                    built_target.result.target_name));
                if (!no_fail_fast) {
                    return std::unexpected(util::make_error(std::format(
                        "Test Error: {} failed with exit code {}.",
                        built_target.result.target_name, run_result.exit_code)));
                }
            }

            if (show_progress) {
                util::output::progress_begin(util::output::ProgressState{
                    .phase = "Running",
                    .completed = executed_tests,
                    .total = total_tests,
                    .current = display_path,
                });
            }
        }
    }

    if (no_run) {
        util::output::argo_status(
            "Finished", util::output::Color::Green,
            std::format("`{}` profile {} {} target(s) built in {:.2f}s",
                        commands::common::profile_name(release),
                        commands::common::profile_descriptor(release),
                        built_count, commands::common::elapsed_seconds(start)));
        return util::Ok;
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
            "Test Error: {} test executable(s) failed: {}.",
            failed_targets.size(), joined));
    }

    util::output::argo_status(
        "Finished", util::output::Color::Green,
        std::format("`{}` profile {} {} runnable target(s) in {:.2f}s",
                    commands::common::profile_name(release),
                    commands::common::profile_descriptor(release),
                    executed_tests, commands::common::elapsed_seconds(start)));
    return util::Ok;
}

}  // namespace cli
