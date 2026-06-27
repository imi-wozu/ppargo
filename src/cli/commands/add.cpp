#include "cli/commands/commands.hpp"
#include "cli/commands/common.hpp"
#include "package/manager.hpp"
#include "util/output.hpp"

namespace cli {

auto AddCommand::execute() const -> util::Status {
    auto context = GUARD(commands::common::load_project_context(std::nullopt));
    util::output::argo_status("Adding", util::output::Color::Green,
                              std::format("{} to dependencies", package));
    GUARD(package::add_dependency(context.project.root, package));
    util::output::argo_status("Added", util::output::Color::Green,
                              std::format("{} to dependencies", package));
    return util::Ok;
}

}  // namespace cli
