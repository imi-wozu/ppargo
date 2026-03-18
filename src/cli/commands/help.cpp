#include "cli/help.hpp"

#include "cli/commands/commands.hpp"
#include "util/output.hpp"

namespace cli {

auto HelpCommand::execute() const -> util::Status {
    util::output::write_help_text(help_text(topic));
    return util::Ok;
}

}  // namespace cli
