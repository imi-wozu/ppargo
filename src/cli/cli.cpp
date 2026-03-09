#include "cli/cli.hpp"

#include "cli/commands/commands.hpp"
#include "cli/parser.hpp"

namespace cli {

auto run(std::span<char*> args) -> util::Status {
    auto parsed = GUARD(parse(args));
    GUARD(std::visit([](auto&& cmd) { return cmd.execute(); }, parsed));
    return util::Ok;
}

}  // namespace cli
