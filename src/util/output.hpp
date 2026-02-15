#pragma once

#include <string_view>

namespace util::output {

enum class Color { Red, Green, Yellow, Cyan, Default };
enum class Stream { Stdout, Stderr };

auto use_color(Stream stream) -> bool;
auto argo_status(std::string_view label, Color color,
                 std::string_view message) -> void;
auto error(std::string_view message) -> void;
auto warning(std::string_view message) -> void;
auto info(std::string_view message) -> void;
auto line(Stream stream, std::string_view message) -> void;
auto write_help(std::string_view help_text) -> void;
auto trace(std::string_view message) -> void;

}  // namespace util::output
