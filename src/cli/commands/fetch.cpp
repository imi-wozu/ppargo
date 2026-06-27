#include "cli/commands/commands.hpp"
#include "cli/commands/common.hpp"
#include "package/manager.hpp"
#include "util/output.hpp"

namespace cli {

auto FetchCommand::execute() const -> util::Status {
    auto context = GUARD(commands::common::load_project_context(std::nullopt));
    util::output::info("Fetching dependencies");
    GUARD(package::fetch_dependencies(context.project));
    util::output::info("Fetched dependencies");
    return util::Ok;
}

}  // namespace cli
