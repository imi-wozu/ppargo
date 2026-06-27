#include "registry/reference_support.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace registry::reference::detail {

namespace {

auto skip_json_ws(std::string_view json, std::size_t index) -> std::size_t {
    while (index < json.size() &&
           std::isspace(static_cast<unsigned char>(json[index])) != 0) {
        ++index;
    }
    return index;
}

auto find_json_value_start(std::string_view json, std::string_view field)
    -> std::optional<std::size_t> {
    const auto needle = std::string{"\""} + std::string(field) + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    position += needle.size();
    position = skip_json_ws(json, position);
    if (position >= json.size() || json[position] != ':') {
        return std::nullopt;
    }
    ++position;
    position = skip_json_ws(json, position);
    if (position >= json.size()) {
        return std::nullopt;
    }
    return position;
}

auto parse_json_string_at(std::string_view json, std::size_t start)
    -> std::optional<std::pair<std::string, std::size_t>> {
    if (start >= json.size() || json[start] != '"') {
        return std::nullopt;
    }

    std::string value;
    bool escaping = false;
    for (std::size_t index = start + 1; index < json.size(); ++index) {
        const auto ch = json[index];
        if (escaping) {
            switch (ch) {
                case '"':
                case '\\':
                case '/':
                    value.push_back(ch);
                    break;
                case 'b':
                    value.push_back('\b');
                    break;
                case 'f':
                    value.push_back('\f');
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    value.push_back(ch);
                    break;
            }
            escaping = false;
            continue;
        }

        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (ch == '"') {
            return std::pair<std::string, std::size_t>{std::move(value),
                                                       index + 1};
        }
        value.push_back(ch);
    }

    return std::nullopt;
}

}  // namespace

auto extract_json_string_field(std::string_view json, std::string_view field)
    -> std::optional<std::string> {
    const auto start = find_json_value_start(json, field);
    if (!start.has_value()) {
        return std::nullopt;
    }
    const auto parsed = parse_json_string_at(json, *start);
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    return parsed->first;
}

auto extract_json_bool_field(std::string_view json, std::string_view field)
    -> std::optional<bool> {
    const auto start = find_json_value_start(json, field);
    if (!start.has_value()) {
        return std::nullopt;
    }
    if (json.substr(*start, 4) == "true") {
        return true;
    }
    if (json.substr(*start, 5) == "false") {
        return false;
    }
    return std::nullopt;
}

auto extract_json_string_array_field(std::string_view json,
                                     std::string_view field)
    -> std::optional<std::vector<std::string>> {
    const auto start = find_json_value_start(json, field);
    if (!start.has_value() || json[*start] != '[') {
        return std::nullopt;
    }

    std::vector<std::string> values;
    std::size_t index = *start + 1;
    while (index < json.size()) {
        index = skip_json_ws(json, index);
        if (index >= json.size()) {
            return std::nullopt;
        }
        if (json[index] == ']') {
            return values;
        }

        const auto parsed = parse_json_string_at(json, index);
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        values.push_back(parsed->first);
        index = skip_json_ws(json, parsed->second);
        if (index < json.size() && json[index] == ',') {
            ++index;
            continue;
        }
        if (index < json.size() && json[index] == ']') {
            return values;
        }
        return std::nullopt;
    }

    return std::nullopt;
}

}  // namespace registry::reference::detail
