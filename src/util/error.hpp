#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace util {

struct Error {
    std::string message;

    Error() = default;

    explicit Error(std::string message_value)
        : message(std::move(message_value)) {}

    auto empty() const -> bool { return message.empty(); }
    auto c_str() const -> const char* { return message.c_str(); }
    auto find(std::string_view value, std::size_t pos = 0) const
        -> std::size_t {
        return message.find(value, pos);
    }

    operator std::string_view() const noexcept { return message; }
};

inline auto make_error(std::string message) -> Error {
    return Error{std::move(message)};
}

inline auto make_unexpected(std::string message) -> std::unexpected<Error> {
    return std::unexpected(make_error(std::move(message)));
}

}  // namespace util
