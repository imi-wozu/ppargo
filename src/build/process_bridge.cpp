#include "build/process_bridge.hpp"

#include <cstdlib>

#include "util/process.hpp"
#include "util/output.hpp"


namespace {

[[maybe_unused]] auto quote_string(const std::string& value) {
    return "\"" + value + "\"";
}

auto trace_enabled() {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&value, &len, "PPARGO_TRACE") != 0 || value == nullptr) {
        return false;
    }
    const bool enabled = len > 0;
    std::free(value);
    return enabled;
#else
    return std::getenv("PPARGO_TRACE") != nullptr;
#endif
}

}  // namespace

namespace build::process_bridge {

auto run(const std::filesystem::path& program,
         const std::vector<std::string>& args) -> util::Result<int> {
    if (trace_enabled()) {
        std::string command_line = quote_string(program.string());
        for (const auto& arg : args) {
            command_line += " " + quote_string(arg);
        }
        util::output::trace(command_line);
    }

    auto result = util::process::run_result(program, args);
    if (!result) {
        return std::unexpected(result.error());
    }
    return result->exit_code;
}

}  // namespace build::process_bridge



