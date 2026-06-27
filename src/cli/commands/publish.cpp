#include "cli/commands/commands.hpp"
#include "cli/commands/common.hpp"
#include "package/manager.hpp"
#include "util/output.hpp"
#include "util/result.hpp"















namespace cli {

auto PublishCommand::execute() const -> util::Status {
    auto context = GUARD(commands::common::load_project_context(std::nullopt));
    util::output::info("Publishing package");
    GUARD(package::publish_package(context.project.root));
    util::output::info("Published package");
    return util::Ok;
}

}  // namespace cli




