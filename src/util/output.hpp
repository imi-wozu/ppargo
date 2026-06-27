#pragma once

#include <cstddef>
#include <string_view>

#include "util/error.hpp"

namespace util::output {

enum class Color { Red, Green, Yellow, Cyan, Default };
enum class Stream { Stdout, Stderr };
enum class ColorMode { Auto, Always, Never };

struct OutputOptions {
    bool quiet = false;
    ColorMode color_mode = ColorMode::Auto;
};

struct ProgressState {
    std::string_view phase;
    std::size_t completed = 0;
    std::size_t total = 0;
    std::string_view current;
};

auto use_color(Stream stream) -> bool;
auto progress_supported() -> bool;
auto init(const OutputOptions& options) -> void;
auto reset_for_tests() -> void;
auto progress_begin(const ProgressState& state) -> void;
auto progress_update(const ProgressState& state) -> void;
auto progress_finish() -> void;
auto argo_status(std::string_view label, Color color,
                 std::string_view message) -> void;
auto error(Error e) -> void;
auto warning(std::string_view message) -> void;
auto info(std::string_view message) -> void;
auto line(Stream stream, std::string_view message) -> void;
auto child_diagnostic_line(std::string_view message, bool fatal) -> void;
auto write_help_text(std::string_view help_text) -> void;
auto trace(std::string_view message) -> void;

}  // namespace util::output
