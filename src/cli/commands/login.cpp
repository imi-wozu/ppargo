#include "cli/commands/commands.hpp"
#include "cli/commands/common.hpp"
#include "package/manager.hpp"
#include "util/output.hpp"
#include "util/result.hpp"

















namespace cli {

auto LoginCommand::execute() const -> util::Status {
    auto context = GUARD(commands::common::load_project_context(std::nullopt));
    if (token.empty()) {
        return std::unexpected(util::make_error("Usage Error: login requires --token <TOKEN>."));
    }

    const auto target_registry = registry.empty() ? std::string("ppargo") : registry;
    GUARD(package::auth_login(context.project.root, target_registry, token));
    util::output::info(std::format("Stored login token for registry {}", target_registry));
    return util::Ok;
}

}  // namespace cli







