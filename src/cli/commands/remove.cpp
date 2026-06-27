#include "cli/commands/commands.hpp"
#include "cli/commands/common.hpp"
#include "package/manager.hpp"
#include "util/output.hpp"
#include "util/result.hpp"

















namespace cli {

auto RemoveCommand::execute() const -> util::Status {
    auto context = GUARD(commands::common::load_project_context(std::nullopt));
    util::output::info(std::format("Removing {} from dependencies", package));
    GUARD(package::remove_dependency(context.project.root, package));
    util::output::info(std::format("Removed {} from dependencies", package));
    return util::Ok;
}

}  // namespace cli




