#include "cli/commands/commands.hpp"
#include "cli/commands/common.hpp"
#include "package/manager.hpp"
#include "util/output.hpp"
#include "util/result.hpp"

namespace cli {

auto UpdateCommand::execute() const -> util::Status {
    auto context = GUARD(commands::common::load_project_context(std::nullopt));
    if (package.has_value()) {
        util::output::info(std::format("Updating dependency {}", *package));
    } else {
        util::output::info("Updating dependencies");
    }
    GUARD(package::update_dependencies(context.project.root, package));
    util::output::info("Dependencies updated");
    return util::Ok;
}

}  // namespace cli
