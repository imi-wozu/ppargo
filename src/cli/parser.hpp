#pragma once

#include <span>

#include "cli/commands/commands.hpp"
#include "util/output.hpp"
#include "util/result.hpp"

namespace cli {

auto parse(std::span<char*> args) -> util::Result<ParsedCommand>;
auto parse_output_options(std::span<char*> args) -> util::output::OutputOptions;

}  // namespace cli
