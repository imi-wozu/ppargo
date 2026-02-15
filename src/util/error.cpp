#include "util/error.hpp"

#include "util/output.hpp"

namespace util {

auto print_error(const std::exception& e) -> void {
    output::error(e.what());
}

auto print_error(const std::string& message) -> void {
    output::error(message);
}

}  // namespace util
