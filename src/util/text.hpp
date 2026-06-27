#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "util/result.hpp"

namespace util::text {

inline auto trim_ascii_view(std::string_view value) -> std::string_view {
    std::size_t start = 0;
    while (start < value.size() &&
           std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(start, end - start);
}

inline auto trim_ascii_copy(std::string_view value) -> std::string {
    return std::string(trim_ascii_view(value));
}

inline auto split_tab_fields(std::string_view line) -> std::vector<std::string> {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= line.size()) {
        const auto end = line.find('\t', start);
        const auto part_end = end == std::string_view::npos ? line.size() : end;
        parts.emplace_back(line.substr(start, part_end - start));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

inline auto parse_bool_literal(std::string_view input) -> util::Result<bool> {
    const auto value = trim_ascii_copy(input);
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    return std::unexpected(util::make_error("Invalid boolean value: " + value));
}

}  // namespace util::text
