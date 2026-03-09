#pragma once

#include <span>

#include "util/result.hpp"

namespace cli {
auto run(std::span<char*> args) -> util::Status;
}
