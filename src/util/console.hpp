#pragma once

#include <cstdio>
#include <string>

#include "util/output.hpp"

namespace util {

using Color = output::Color;

auto use_color_stdout() -> bool;
auto use_color_stderr() -> bool;
auto print_status(std::FILE* out, bool use_color, const std::string& label,
                  Color color, const std::string& message) -> void;
auto print_argo_status(std::FILE* out, bool use_color, const std::string& label,
                       Color color, const std::string& message) -> void;

}  // namespace util
