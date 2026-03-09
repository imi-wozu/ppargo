#pragma once

#include <span>

#include "cli/commands/commands.hpp"
#include "util/result.hpp"

namespace cli {

auto parse(std::span<char*> args) -> util::Result<ParsedCommand>;

}  // namespace cli
