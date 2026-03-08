#include "cli/commands/commands.hpp"
#include "core/scaffold.hpp"
#include "util/error.hpp"
#include "util/output.hpp"
#include "util/result.hpp"

namespace cli {

auto InitCommand::execute() const -> util::Status {
    std::error_code ec;
    const std::filesystem::path current = std::filesystem::current_path(ec);
    if (ec) {
        return util::make_unexpected(ec.message());
    }

    const auto dir_name = current.filename().string();
    if (dir_name.empty()) {
        return util::make_unexpected(
            "Cannot determine project name from directory.");
    }

    GUARD(core::validate_project_name(dir_name));
    GUARD(core::create_project_structure(current, dir_name));
    util::output::argo_status(
        "Creating", util::output::Color::Green,
        std::format("binary (application) `{}` package", dir_name));
    return util::Ok;
}

}  // namespace cli
