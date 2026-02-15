#include "cli/cli.hpp"

#include <algorithm>

#include "cli/commands/commands.hpp"
#include "cli/parser.hpp"

namespace cli {

auto run(int argc, char* argv[]) -> util::Status {
    return parse(argc, argv).and_then([](auto&& parsed) {
        return std::visit([](auto&& cmd) { return cmd.execute(); }, parsed);
    });
}

}  // namespace cli
