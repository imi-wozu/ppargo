#include "core/templates.hpp"

namespace core::templates {

auto main_cpp_template() -> std::string {
    return R"(#include <print>

auto main() -> int {
    std::println("Hello, world!");
    return 0;
}
)";
}

auto gitignore_template() -> std::string { return "/target\n/packages\n"; }

}  // namespace core::templates
