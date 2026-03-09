#pragma once

#include <expected>
#include <string>
#include <utility>

namespace util {

struct Error {
    std::string message;

    Error() = default;

    explicit Error(std::string message_value)
        : message(std::move(message_value)) {}
};

inline auto make_error(std::string message) -> Error {
    return Error{std::move(message)};
}

inline auto make_unexpected(std::string message) -> std::unexpected<Error> {
    return std::unexpected(make_error(std::move(message)));
}

}  // namespace util
