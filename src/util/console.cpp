#include "util/console.hpp"

#include <format>

namespace util {

auto use_color_stdout() -> bool {
    return output::use_color(output::Stream::Stdout);
}

auto use_color_stderr() -> bool {
    return output::use_color(output::Stream::Stderr);
}

auto print_status(std::FILE* out, bool use_color, const std::string& label,
                  Color color, const std::string& message) -> void {
    (void)use_color;
    if (label == "error:") {
        output::error(message);
        return;
    }
    if (label == "warning:") {
        output::warning(message);
        return;
    }

    const auto stream =
        out == stderr ? output::Stream::Stderr : output::Stream::Stdout;
    if (color == Color::Default || label.empty()) {
        output::line(stream, message);
    } else if (message.empty()) {
        output::line(stream, label);
    } else {
        output::line(stream, std::format("{} {}", label, message));
    }
}

auto print_argo_status(std::FILE* out, bool use_color, const std::string& label,
                       Color color, const std::string& message) -> void {
    (void)out;
    (void)use_color;
    output::argo_status(label, color, message);
}

}  // namespace util
