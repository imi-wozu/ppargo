#include "cli/commands/commands.hpp"

#include <filesystem>
#include <format>
#include <system_error>

#include "core/manifest.hpp"
#include "core/paths.hpp"
#include "util/output.hpp"
#include "util/process.hpp"
#include "util/result.hpp"


namespace cli {

auto RunCommand::execute() const -> util::Status {
    TRY_void(BuildCommand{.release = release}.execute());
    auto root = TRY(core::find_project_root());
    auto manifest = TRY(core::load_manifest(root / "ppargo.toml"));

    const auto executable =
        core::build_dir(root, manifest, release) / core::binary_name(manifest);

    std::error_code ec;
    if (!std::filesystem::exists(executable, ec) || ec) {
        return std::unexpected(
            std::format("Executable not found: {}", executable.string()));
    }

    const auto relative_exe = std::filesystem::relative(executable, root, ec);
    const auto display_path =
        ec ? executable.generic_string() : relative_exe.generic_string();
    util::output::argo_status("Running", util::output::Color::Green,
                              std::format("`{}`", display_path));

    util::process::RunOptions options;
    options.working_directory = root;
    auto result = TRY(util::process::run_result(executable, {}, options));
    if (result.exit_code != 0) {
        return std::unexpected(
            std::format("Process exited with non-zero status: {}", result.exit_code));
    }

    return util::Ok;
}

}  // namespace cli



