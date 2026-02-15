#include "cli/commands/commands.hpp"

#include <chrono>
#include <format>

#include "build/manager.hpp"
#include "core/manifest.hpp"
#include "core/paths.hpp"
#include "util/output.hpp"
#include "util/result.hpp"

namespace cli {

auto CheckCommand::execute() const -> util::Status {
    auto root = TRY(core::find_project_root());
    auto manifest = TRY(core::load_manifest(root / "ppargo.toml"));

    util::output::argo_status(
        "Checking", util::output::Color::Green,
        std::format("{} v{} ({})", manifest.package.name, manifest.package.version,
                    root.string()));

    const auto start = std::chrono::steady_clock::now();
    build::BuildOptions options;
    options.release = release;
    options.mode = build::BuildOptions::Mode::Check;
    auto ignored = TRY(build::execute(root, manifest, options));
    (void)ignored;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();

    util::output::argo_status(
        "Finished", util::output::Color::Green,
        std::format("`{}` profile {} target(s) in {:.2f}s",
                    release ? "release" : "dev",
                    release ? "[optimized]" : "[unoptimized + debuginfo]",
                    elapsed / 1000.0));
    return util::Ok;
}

}  // namespace cli
