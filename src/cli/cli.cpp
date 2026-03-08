#include "cli/cli.hpp"

#include <algorithm>

#include "cli/commands/commands.hpp"
#include "cli/parser.hpp"

namespace cli {

auto run(int argc, char* argv[]) -> util::Status {
    auto parsed = GUARD(parse(argc, argv));
    GUARD(std::visit([](auto&& cmd) { return cmd.execute(); }, parsed));
    return util::Ok;
}

}  // namespace cli