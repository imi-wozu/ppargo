#pragma once

#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "util/error.hpp"

namespace util {

template <typename T>
using Result = std::expected<T, Error>;
using Status = std::expected<void, util::Error>;

struct OkToken {
    [[nodiscard]] auto status() const -> Status { return Status{}; }

    [[nodiscard]] operator Status() const { return status(); }

    [[nodiscard]] auto operator()() const -> Status { return status(); }

    template <typename T>
    [[nodiscard]] auto operator()(T&& value) const -> Result<std::decay_t<T>> {
        using Value = std::decay_t<T>;
        return Result<Value>{std::forward<T>(value)};
    }
};

inline constexpr OkToken Ok{};

template <typename T>
inline auto discard_result(Result<T>&& result) -> Status {
    if (!result) {
        return std::unexpected(std::move(result.error()));
    }
    return Ok;
}

template <typename T>
inline auto status_from(Result<T>&& result) -> Status {
    return discard_result(std::move(result));
}

}  // namespace util

#define CONCAT_INNER(a, b) a##b
#define CONCAT(a, b) CONCAT_INNER(a, b)

#if defined(__INTELLISENSE__)
// VS Code IntelliSense does not parse GNU statement expressions.
#define GUARD(expr) ((expr).value())
#else
#define GUARD_IMPL(id, expr)                                              \
    ({                                                                    \
        auto CONCAT(_tmp_, id) = (expr);                                  \
        if (!CONCAT(_tmp_, id)) {                                         \
            return std::unexpected(std::move(CONCAT(_tmp_, id).error())); \
        }                                                                 \
        std::move(CONCAT(_tmp_, id)).value();                             \
    })

#define GUARD(expr) GUARD_IMPL(__COUNTER__, expr)
#endif