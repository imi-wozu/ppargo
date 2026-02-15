#include "cli/commands/commands.hpp"

#include <filesystem>
#include <format>

#include "core/scaffold.hpp"
#include "util/output.hpp"
#include "util/result.hpp"


namespace cli {

auto NewCommand::execute() const -> util::Status {
    TRY_void(core::validate_project_name(name));
    TRY_void(core::create_project_structure(std::filesystem::path(name), name));
    util::output::argo_status("Creating", util::output::Color::Green,
                              std::format("binary (application) `{}` package", name));
    return util::Ok;
}

}  // namespace cli


