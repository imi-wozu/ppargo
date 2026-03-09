#include "cli/commands/commands.hpp"
#include "core/scaffold.hpp"
#include "util/output.hpp"

namespace cli {

auto NewCommand::execute() const -> util::Status {
    GUARD(core::validate_project_name(name));
    GUARD(core::create_project_structure(std::filesystem::path(name), name));
    util::output::argo_status(
        "Creating", util::output::Color::Green,
        std::format("binary (application) `{}` package", name));
    return util::Ok;
}

}  // namespace cli
