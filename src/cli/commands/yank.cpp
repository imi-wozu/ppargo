#include "cli/commands/commands.hpp"
#include "cli/commands/common.hpp"
#include "package/manager.hpp"
#include "util/output.hpp"
#include "util/result.hpp"

namespace cli {

auto YankCommand::execute() const -> util::Status {
    auto context = GUARD(commands::common::load_project_context(std::nullopt));
    util::output::info(
        std::format("{} version {}", undo ? "Unyanking" : "Yanking", version));
    GUARD(package::yank_package(context.project.root, version, undo));
    util::output::info(
        std::format("{}ed version {}", undo ? "Unyank" : "Yank", version));
    return util::Ok;
}

}  // namespace cli
