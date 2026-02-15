#pragma once

#include <exception>
#include <string>

namespace util {
auto print_error(const std::exception& e) -> void;
auto print_error(const std::string& message) -> void;
}
