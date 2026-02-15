#pragma once

#include <string>

#include "cli/commands/commands.hpp"
#include "util/result.hpp"

namespace cli {

auto parse(int argc, char* argv[]) -> util::Result<ParsedCommand>;
}  // namespace cli
