#pragma once

#include "util/result.hpp"

namespace cli {
auto run(int argc, char* argv[]) -> util::Status;
}
