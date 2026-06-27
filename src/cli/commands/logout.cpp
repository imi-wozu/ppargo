#include "cli/commands/commands.hpp"
#include "cli/commands/common.hpp"
#include "package/manager.hpp"
#include "util/output.hpp"
#include "util/result.hpp"

















namespace cli {

auto LogoutCommand::execute() const -> util::Status {
    auto context = GUARD(commands::common::load_project_context(std::nullopt));
    const auto target_registry = registry.empty() ? std::string("ppargo") : registry;
    GUARD(package::auth_logout(context.project.root, target_registry));
    util::output::info(std::format("Removed login token for registry {}", target_registry));
    return util::Ok;
}

}  // namespace cli




