#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <system_error>
#include <utility>

#include "build/manager.hpp"
#include "build/targets.hpp"
#include "cli/commands/commands.hpp"
#include "cli/commands/common.hpp"
#include "package/manager.hpp"
#include "util/output.hpp"
#include "util/process.hpp"
#include "util/result.hpp"

namespace cli {

namespace {

auto make_package_selection_options(const RunCommand& command)
    -> commands::common::PackageSelectionOptions {
    return commands::common::PackageSelectionOptions{
        .workspace = command.workspace,
        .all = command.all_packages,
        .packages = command.packages,
        .excludes = command.exclude_packages,
    };
}

}  // namespace

auto RunCommand::execute() const -> util::Status {
    commands::common::ScopedCompileEnvironment scoped_environment(
        commands::common::compile_environment_settings(jobs, verbose));

    auto contexts = GUARD(commands::common::prepare_build_command_contexts(
        manifest_path, target_dir,
        commands::common::make_dependency_workflow_options(locked, offline,
                                                           frozen, features,
                                                           all_features,
                                                           no_default_features),
        make_package_selection_options(*this)));
    const auto selection{commands::common::make_target_selection(bin, example)};

    for (auto& context : contexts) {
        if (selection.has_value()) {
            commands::common::log_selected_targets(
                context.project, verbose, "running",
                std::span<const build::targets::Selection>(&*selection, 1));
        }

        auto dependency_artifacts = GUARD(package::build_dependency_artifacts(
            context.project.project, context.dependencies));
        build::BuildOptions build_options{
            .release = release,
            .mode = build::BuildOptions::Mode::Build,
            .target = selection,
            .output_dir_override = context.project.output_dir_override,
            .dependency_artifacts = std::move(dependency_artifacts),
        };
        auto build_result =
            GUARD(build::execute(context.project.project.root,
                                 context.project.project.manifest, build_options));
        const auto executable{build_result.output_binary};

        std::error_code ec;
        if (!std::filesystem::exists(executable, ec) || ec) {
            return std::unexpected(util::make_error(
                std::format("Executable not found: {}", executable.string())));
        }

        const auto relative_exe = std::filesystem::relative(
            executable, context.project.project.root, ec);
        const auto display_path =
            ec ? executable.generic_string() : relative_exe.generic_string();
        util::output::argo_status("Running", util::output::Color::Green,
                                  std::format("`{}`", display_path));

        util::process::RunOptions options{};
        options.working_directory = context.project.project.root;
        auto result = GUARD(
            util::process::run_result(executable, passthrough_args, options));
        if (result.exit_code != 0) {
            return std::unexpected(util::make_error(std::format(
                "Process exited with non-zero status: {}", result.exit_code)));
        }
    }

    return util::Ok;
}

}  // namespace cli
