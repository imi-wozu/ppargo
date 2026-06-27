#include "cli/commands/commands.hpp"
#include "cli/commands/common.hpp"
#include "package/manager.hpp"
#include "util/output.hpp"
#include "util/result.hpp"

















namespace cli {

auto OwnerCommand::execute() const -> util::Status {
    auto context = GUARD(commands::common::load_project_context(std::nullopt));
    if (add_owner.has_value() == remove_owner.has_value()) {
        return std::unexpected(util::make_error("Usage Error: owner command requires exactly one of --add <OWNER> or --remove <OWNER>."));
    }

    if (add_owner.has_value()) {
        util::output::info(std::format("Adding owner {}", *add_owner));
        GUARD(package::add_owner(context.project.root, *add_owner));
        util::output::info(std::format("Added owner {}", *add_owner));
        return util::Ok;
    }

    util::output::info(std::format("Removing owner {}", *remove_owner));
    GUARD(package::remove_owner(context.project.root, *remove_owner));
    util::output::info(std::format("Removed owner {}", *remove_owner));
    return util::Ok;
}

}  // namespace cli







