#include "cli/commands/commands.hpp"

#include <format>

#include "core/paths.hpp"
#include "package/manager.hpp"
#include "util/output.hpp"
#include "util/result.hpp"

namespace cli {

auto AddCommand::execute() const -> util::Status {
    auto root = TRY(core::find_project_root());
    util::output::info(std::format("Adding {} to dependencies", package));
    TRY_void(package::add_dependency(root, package));
    util::output::info(std::format("Added {} to dependencies", package));
    return util::Ok;
}

}  // namespace cli
