#include "util/output.hpp"

#include <cstdio>
#include <mutex>
#include <print>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "util/environment.hpp"

namespace {

struct ProgressRuntime {
    bool active = false;
    bool suppressed = false;
    std::size_t last_width = 0;
    std::string phase;
    std::size_t completed = 0;
    std::size_t total = 0;
    std::string current;
};

struct InvocationRuntime {
    util::output::OutputOptions options{};
};

auto selected_file(util::output::Stream stream) -> std::FILE* {
    return stream == util::output::Stream::Stdout ? stdout : stderr;
}

auto output_mutex() -> std::mutex& {
    static std::mutex mutex;
    return mutex;
}

auto progress_runtime() -> ProgressRuntime& {
    static ProgressRuntime runtime;
    return runtime;
}

auto invocation_runtime() -> InvocationRuntime& {
    static InvocationRuntime runtime;
    return runtime;
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

auto is_help_heading(std::string_view line) -> bool {
    return line == "Usage:" || line.starts_with("Usage:") ||
           line == "Commands:" || line == "Command:" || line == "Arguments:" ||
           line == "Argument:" || line == "Options:" || line == "Option:" ||
           line.ends_with(" Options:") || line.ends_with(" Selection:") ||
           line == "Default behavior:";
}

auto colorize_usage_line(util::output::Stream stream, std::string_view line)
    -> std::string {
    constexpr std::string_view kUsagePrefix = "Usage:";
    if (!line.starts_with(kUsagePrefix)) {
        return std::string(line);
    }

    if (!util::output::use_color(stream)) {
        return std::string(line);
    }

    const auto remainder = line.substr(kUsagePrefix.size());
    if (remainder.empty()) {
        return std::format("\x1b[1m\x1b[32m{}\x1b[0m", kUsagePrefix);
    }

    std::string colored = std::format("\x1b[1m\x1b[32m{}\x1b[0m", kUsagePrefix);
    std::size_t start = 0;
    while (start < remainder.size()) {
        const auto bracket_start = remainder.find_first_of("[<", start);
        if (bracket_start == std::string_view::npos) {
            if (start < remainder.size()) {
                colored +=
                    std::format("\x1b[96m{}\x1b[0m", remainder.substr(start));
            }
            break;
        }

        if (bracket_start > start) {
            colored +=
                std::format("\x1b[96m{}\x1b[0m",
                            remainder.substr(start, bracket_start - start));
        }

        const auto open = remainder[bracket_start];
        const auto close = open == '[' ? ']' : '>';
        const auto bracket_end = remainder.find(close, bracket_start + 1);
        if (bracket_end == std::string_view::npos) {
            colored += std::format("\x1b[34m{}\x1b[0m",
                                   remainder.substr(bracket_start));
            break;
        }

        colored += std::format(
            "\x1b[34m{}\x1b[0m",
            remainder.substr(bracket_start, bracket_end - bracket_start + 1));
        start = bracket_end + 1;
    }

    return colored;
}

auto colorize_help_token(std::string_view line) -> std::string {
    std::string colored;
    colored.reserve(line.size() + 32);

    std::size_t start = 0;
    while (start < line.size()) {
        const auto quoted = line.find("'--help'", start);
        const auto plain = line.find("--help", start);

        std::size_t next = std::string_view::npos;
        bool use_quoted = false;
        if (quoted != std::string_view::npos &&
            (plain == std::string_view::npos || quoted <= plain)) {
            next = quoted;
            use_quoted = true;
        } else if (plain != std::string_view::npos) {
            next = plain;
        }

        if (next == std::string_view::npos) {
            colored.append(line.substr(start));
            break;
        }

        colored.append(line.substr(start, next - start));
        const auto token = use_quoted ? std::string_view("'--help'")
                                      : std::string_view("--help");
        colored += std::format("\x1b[96m{}\x1b[0m", token);
        start = next + token.size();
    }

    return colored;
}

auto colorize_command_names(std::string_view text) -> std::string {
    std::string colored;
    colored.reserve(text.size() + 32);

    std::size_t start = 0;
    while (start < text.size()) {
        const auto token_start = text.find_first_of(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_+-",
            start);
        if (token_start == std::string_view::npos) {
            colored.append(text.substr(start));
            break;
        }

        colored.append(text.substr(start, token_start - start));

        auto token_end = token_start;
        while (token_end < text.size()) {
            const char ch = text[token_end];
            const bool is_token_char =
                (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                (ch >= '0' && ch <= '9') || ch == '_' || ch == '+' || ch == '-';
            if (!is_token_char) {
                break;
            }
            ++token_end;
        }

        colored +=
            std::format("\x1b[96m{}\x1b[0m",
                        text.substr(token_start, token_end - token_start));
        start = token_end;
    }

    return colored;
}

auto colorize_help_entry_label(std::string_view line) -> std::string {
    const auto label_start = line.find_first_not_of(' ');
    if (label_start == std::string_view::npos) {
        return std::string(line);
    }

    if (label_start == 0) {
        return colorize_help_token(line);
    }

    const auto first = line[label_start];
    const bool is_argument_label = first == '<' || first == '[';
    const bool is_option_label = first == '-';
    const bool is_command_label =
        (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z');

    if (!is_argument_label && !is_option_label && !is_command_label) {
        return colorize_help_token(line);
    }

    const auto label_end = line.find("  ", label_start);

    if (is_command_label) {
        const auto label =
            label_end == std::string_view::npos
                ? line.substr(label_start)
                : line.substr(label_start, label_end - label_start);
        std::size_t command_end = label.size();
        if (const auto arg_begin = label.find(" <");
            arg_begin != std::string_view::npos) {
            command_end = arg_begin;
        }
        if (const auto option_begin = label.find(" [");
            option_begin != std::string_view::npos &&
            option_begin < command_end) {
            command_end = option_begin;
        }

        const auto colored_commands =
            colorize_command_names(label.substr(0, command_end));
        if (label_end == std::string_view::npos) {
            return std::format("{}{}{}", line.substr(0, label_start),
                               colored_commands, label.substr(command_end));
        }

        return std::format("{}{}{}{}", line.substr(0, label_start),
                           colored_commands, label.substr(command_end),
                           colorize_help_token(line.substr(label_end)));
    }

    if (label_end == std::string_view::npos) {
        return std::format("{}\x1b[96m{}\x1b[0m", line.substr(0, label_start),
                           line.substr(label_start));
    }

    return std::format("{}\x1b[96m{}\x1b[0m{}", line.substr(0, label_start),
                       line.substr(label_start, label_end - label_start),
                       colorize_help_token(line.substr(label_end)));
}

auto colorize_structured_line(util::output::Stream stream,
                              std::string_view line) -> std::string {
    if (!util::output::use_color(stream)) {
        return std::string(line);
    }
    if (line.starts_with(std::string_view("Usage:"))) {
        return colorize_usage_line(stream, line);
    }
    if (!is_help_heading(line)) {
        return colorize_help_entry_label(line);
    }
    return std::format("\x1b[1m\x1b[32m{}\x1b[0m", line);
}

void write_multiline_message(std::FILE* out, util::output::Stream stream,
                             std::string_view message) {
    std::size_t start = 0;
    while (start <= message.size()) {
        const auto end = message.find('\n', start);
        auto line_view = message.substr(start, end == std::string_view::npos
                                                   ? message.size() - start
                                                   : end - start);
        if (!line_view.empty() && line_view.back() == '\r') {
            line_view.remove_suffix(1);
        }
        std::print(out, "{}", colorize_structured_line(stream, line_view));
        if (end == std::string_view::npos) {
            break;
        }
        std::print(out, "\n");
        start = end + 1;
    }
}

#ifdef _WIN32
auto enable_vt_mode(util::output::Stream stream) -> bool {
    const HANDLE handle = stream == util::output::Stream::Stdout
                              ? GetStdHandle(STD_OUTPUT_HANDLE)
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

auto no_color_requested() -> bool { return util::env::exists("NO_COLOR"); }

auto stdout_is_tty() -> bool {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
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

struct RenderedProgressLine {
    std::string rendered;
    std::size_t visible_width = 0;
};

auto progress_bar(std::size_t completed, std::size_t total) -> std::string {
    constexpr std::size_t kBarWidth = 28;
    if (total == 0) {
        return std::string(kBarWidth, ' ');
    }

    if (completed >= total) {
        return std::string(kBarWidth, '=');
    }

    const auto head =
        (std::min)(kBarWidth - 1,
                   static_cast<std::size_t>(
                       (static_cast<double>(completed) / total) * kBarWidth));

    std::string bar(kBarWidth, ' ');
    std::fill_n(bar.begin(), head, '=');
    bar[head] = '>';
    return bar;
}

auto progress_phase_text(std::string_view phase) -> std::string_view {
    if (phase == "Compiling" || phase == "Linking") {
        return "Building";
    }
    return phase;
}

auto render_progress_label(std::string_view label) -> std::string {
    if (!util::output::use_color(util::output::Stream::Stdout)) {
        return std::string(label);
    }
    if (label == "Building") {
        return std::format("\x1b[1m\x1b[96m{}\x1b[0m", label);
    }
    return std::string(label);
}

auto truncate_progress_text(std::string_view text, std::size_t max_width)
    -> std::string {
    if (max_width == 0 || text.empty()) {
        return {};
    }
    if (text.size() <= max_width) {
        return std::string(text);
    }
    if (max_width <= 3) {
        return std::string(max_width, '.');
    }
    return std::string(text.substr(0, max_width - 3)) + "...";
}

auto format_progress_line(const ProgressRuntime& runtime)
    -> RenderedProgressLine {
    constexpr std::size_t kMaxVisibleWidth = 100;
    const auto label = progress_phase_text(runtime.phase);
    const auto bar = progress_bar(runtime.completed, runtime.total);
    const auto prefix = std::format("{} [{}] {}/{}", label, bar,
                                    runtime.completed, runtime.total);

    std::string suffix;
    if (!runtime.current.empty()) {
        const auto reserved = prefix.size() + 2;
        if (kMaxVisibleWidth > reserved) {
            const auto truncated = truncate_progress_text(
                runtime.current, kMaxVisibleWidth - reserved);
            if (!truncated.empty()) {
                suffix = std::format(": {}", truncated);
            }
        }
    }

    const auto visible = prefix + suffix;
    return RenderedProgressLine{
        .rendered = std::format("{} [{}] {}/{}{}", render_progress_label(label),
                                bar, runtime.completed, runtime.total, suffix),
        .visible_width = visible.size(),
    };
}

void clear_progress_visual(std::FILE* out, std::size_t width) {
    if (util::output::use_color(util::output::Stream::Stdout)) {
        std::print(out, "\r\x1b[2K\r");
        return;
    }

    if (width > 0) {
        std::print(out, "\r{:{}}\r", "", width);
        return;
    }

    std::print(out, "\r");
}

void clear_progress_unlocked(bool reset_suppression = false) {
    auto& runtime = progress_runtime();
    if (!runtime.active) {
        if (reset_suppression) {
            runtime.suppressed = false;
        }
        return;
    }

    std::FILE* out = selected_file(util::output::Stream::Stdout);
    clear_progress_visual(out, runtime.last_width);
    std::fflush(out);
    runtime.active = false;
    runtime.last_width = 0;
    runtime.phase.clear();
    runtime.current.clear();
    runtime.completed = 0;
    runtime.total = 0;
    if (reset_suppression) {
        runtime.suppressed = false;
    }
}

auto override_color_enabled(util::output::Stream stream) -> std::optional<bool> {
    switch (invocation_runtime().options.color_mode) {
        case util::output::ColorMode::Auto:
            return std::nullopt;
        case util::output::ColorMode::Never:
            return false;
        case util::output::ColorMode::Always:
#ifdef _WIN32
            return enable_vt_mode(stream);
#else
            return true;
#endif
    }
    return std::nullopt;
}

void render_progress_unlocked() {
    auto& runtime = progress_runtime();
    if (!runtime.active || runtime.suppressed) {
        return;
    }

    const auto line = format_progress_line(runtime);
    const auto width = line.visible_width;
    std::FILE* out = selected_file(util::output::Stream::Stdout);
    clear_progress_visual(out, runtime.last_width);
    std::print(out, "\r{}", line.rendered);
    std::fflush(out);
    runtime.last_width = width;
}

void redraw_progress_unlocked() {
    auto& runtime = progress_runtime();
    if (runtime.suppressed || runtime.phase.empty() || runtime.total == 0) {
        return;
    }

    runtime.active = true;
    render_progress_unlocked();
}

void update_progress_unlocked(const util::output::ProgressState& state) {
    auto& runtime = progress_runtime();
    if (runtime.suppressed) {
        return;
    }
    runtime.active = true;
    runtime.phase = state.phase;
    runtime.completed = state.completed;
    runtime.total = state.total;
    runtime.current = state.current;
    render_progress_unlocked();
}

void finish_progress_before_output(bool suppress_future_updates) {
    auto& runtime = progress_runtime();
    if (runtime.active) {
        clear_progress_unlocked();
    }
    if (suppress_future_updates) {
        runtime.suppressed = true;
    }
}

auto print_status_line(util::output::Stream stream, std::string_view label,
                       util::output::Color color, std::string_view message,
                       bool align_for_argo, bool suppress_future_progress)
    -> void {
    finish_progress_before_output(suppress_future_progress);

    std::FILE* out = selected_file(stream);
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
    if (const auto override = override_color_enabled(stream);
        override.has_value()) {
        return *override;
    }
    static const bool stdout_enabled = supports_color(Stream::Stdout);
    static const bool stderr_enabled = supports_color(Stream::Stderr);
    return stream == Stream::Stdout ? stdout_enabled : stderr_enabled;
}

auto progress_supported() -> bool {
    if (invocation_runtime().options.quiet) {
        return false;
    }
    static const bool enabled =
        stdout_is_tty() && !util::env::exists("PPARGO_TRACE");
    return enabled;
}

auto init(const OutputOptions& options) -> void {
    std::lock_guard<std::mutex> lock(output_mutex());
    invocation_runtime().options = options;
}

auto reset_for_tests() -> void {
    std::lock_guard<std::mutex> lock(output_mutex());
    invocation_runtime().options = OutputOptions{};
    clear_progress_unlocked(true);
}

void progress_begin(const ProgressState& state) {
    if (!progress_supported() || state.total == 0 || state.phase.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(output_mutex());
    update_progress_unlocked(state);
}

void progress_update(const ProgressState& state) {
    if (!progress_supported() || state.total == 0 || state.phase.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(output_mutex());
    update_progress_unlocked(state);
}

void progress_finish() {
    std::lock_guard<std::mutex> lock(output_mutex());
    clear_progress_unlocked(true);
}

auto argo_status(std::string_view label, Color color, std::string_view message)
    -> void {
    if (invocation_runtime().options.quiet) {
        return;
    }
    std::lock_guard<std::mutex> lock(output_mutex());
    print_status_line(Stream::Stdout, label, color, message, true, false);
}

void error(Error e) {
    if (e.message.empty()) {
        e.message = "unknown error";
    }

    std::lock_guard<std::mutex> lock(output_mutex());
    finish_progress_before_output(true);

    std::FILE* out = selected_file(Stream::Stderr);
    const bool color_enabled = use_color(Stream::Stderr);
    if (color_enabled) {
        std::print(out, "\x1b[1m{}error:\x1b[0m", color_code(Color::Red));
    } else {
        std::print(out, "error:");
    }
    if (!e.message.empty()) {
        std::print(out, " ");
        write_multiline_message(out, Stream::Stderr, e.message);
    }
    std::print(out, "\n");
    std::fflush(out);
}

void warning(std::string_view message) {
    std::lock_guard<std::mutex> lock(output_mutex());
    print_status_line(Stream::Stderr, "warning:", Color::Yellow, message, false,
                      false);
}

void info(std::string_view message) {
    if (invocation_runtime().options.quiet) {
        return;
    }
    line(Stream::Stdout, message);
}

void line(Stream stream, std::string_view message) {
    std::lock_guard<std::mutex> lock(output_mutex());
    finish_progress_before_output(false);

    std::FILE* out = selected_file(stream);
    if (!message.empty() && message.back() == '\n') {
        std::print(out, "{}", message);
    } else {
        std::print(out, "{}\n", message);
    }
    std::fflush(out);
}

void child_diagnostic_line(std::string_view message, bool fatal) {
    std::lock_guard<std::mutex> lock(output_mutex());
    finish_progress_before_output(fatal);

    std::FILE* out = selected_file(Stream::Stderr);
    if (!message.empty() && message.back() == '\n') {
        std::print(out, "{}", message);
    } else {
        std::print(out, "{}\n", message);
    }
    std::fflush(out);

    if (!fatal) {
        redraw_progress_unlocked();
    }
}

void write_help_text(std::string_view help_text) {
    std::lock_guard<std::mutex> lock(output_mutex());
    finish_progress_before_output(false);

    std::FILE* out = selected_file(Stream::Stdout);
    std::size_t start = 0;
    while (start < help_text.size()) {
        const auto end = help_text.find('\n', start);
        const auto line_view = help_text.substr(
            start, end == std::string_view::npos ? help_text.size() - start
                                                 : end - start);
        std::print(out, "{}\n",
                   colorize_structured_line(Stream::Stdout, line_view));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    if (help_text.empty()) {
        std::print(out, "\n");
    }
    std::fflush(out);
}

void trace(std::string_view message) { line(Stream::Stderr, message); }

}  // namespace util::output
