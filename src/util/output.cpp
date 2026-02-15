#include "util/output.hpp"

#include <cstdio>
#include <cstdlib>
#include <print>
#include <string_view>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

auto selected_stream(util::output::Stream stream) -> std::FILE* {
    return stream == util::output::Stream::Stdout ? stdout : stderr;
}

auto color_code(util::output::Color color) -> std::string_view {
    switch (color) {
        case util::output::Color::Red:
            return "\x1b[31m";
        case util::output::Color::Green:
            return "\x1b[32m";
        case util::output::Color::Yellow:
            return "\x1b[33m";
        case util::output::Color::Cyan:
            return "\x1b[36m";
        case util::output::Color::Default:
            return "";
    }
    return "";
}

#ifdef _WIN32
auto enable_vt_mode(util::output::Stream stream) -> bool {
    const HANDLE handle =
        stream == util::output::Stream::Stdout ? GetStdHandle(STD_OUTPUT_HANDLE)
                                               : GetStdHandle(STD_ERROR_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
        return false;
    }

    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) {
        return false;
    }

    const DWORD wanted = mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (wanted != mode && !SetConsoleMode(handle, wanted)) {
        return false;
    }
    return true;
}
#endif

auto no_color_requested() -> bool {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&value, &len, "NO_COLOR") != 0 || value == nullptr) {
        return false;
    }
    std::free(value);
    return true;
#else
    return std::getenv("NO_COLOR") != nullptr;
#endif
}

auto supports_color(util::output::Stream stream) -> bool {
    if (no_color_requested()) {
        return false;
    }

#ifdef _WIN32
    const int fd = stream == util::output::Stream::Stdout ? _fileno(stdout)
                                                           : _fileno(stderr);
    if (_isatty(fd) == 0) {
        return false;
    }
    return enable_vt_mode(stream);
#else
    const int fd = stream == util::output::Stream::Stdout ? fileno(stdout)
                                                           : fileno(stderr);
    return isatty(fd) != 0;
#endif
}

auto print_status_line(util::output::Stream stream, std::string_view label,
                       util::output::Color color, std::string_view message,
                       bool align_for_argo) -> void {
    std::FILE* out = selected_stream(stream);
    if (align_for_argo) {
        constexpr int kWidth = 12;
        const int pad = static_cast<int>(label.size()) >= kWidth
                            ? 1
                            : (kWidth - static_cast<int>(label.size()));
        std::print(out, "{:>{}}", "", pad);
    }

    const bool color_enabled = util::output::use_color(stream);
    if (color_enabled && color != util::output::Color::Default) {
        std::print(out, "\x1b[1m{}{}\x1b[0m", color_code(color), label);
    } else {
        std::print(out, "{}", label);
    }

    if (!message.empty()) {
        std::print(out, " {}", message);
    }
    std::print(out, "\n");
    std::fflush(out);
}

}  // namespace

namespace util::output {

auto use_color(Stream stream) -> bool {
    static const bool stdout_enabled = supports_color(Stream::Stdout);
    static const bool stderr_enabled = supports_color(Stream::Stderr);
    return stream == Stream::Stdout ? stdout_enabled : stderr_enabled;
}

auto argo_status(std::string_view label, Color color,
                 std::string_view message) -> void {
    print_status_line(Stream::Stdout, label, color, message, true);
}

auto error(std::string_view message) -> void {
    print_status_line(Stream::Stderr, "error:", Color::Red, message, false);
}

auto warning(std::string_view message) -> void {
    print_status_line(Stream::Stderr, "warning:", Color::Yellow, message, false);
}

auto info(std::string_view message) -> void { line(Stream::Stdout, message); }

auto line(Stream stream, std::string_view message) -> void {
    std::FILE* out = selected_stream(stream);
    if (!message.empty() && message.back() == '\n') {
        std::print(out, "{}", message);
    } else {
        std::print(out, "{}\n", message);
    }
    std::fflush(out);
}

auto write_help(std::string_view help_text) -> void {
    line(Stream::Stdout, help_text);
}

auto trace(std::string_view message) -> void { line(Stream::Stderr, message); }

}  // namespace util::output
