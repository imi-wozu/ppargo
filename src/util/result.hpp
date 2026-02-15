#pragma once

#include <exception>
#include <expected>
#include <string>
#include <type_traits>
#include <utility>

namespace util {

template <typename T>
using Result = std::expected<T, std::string>;

using Status = std::expected<void, std::string>;

inline const Status Ok{};

inline auto Error(const char* msg) -> Status {
    return std::unexpected<std::string>(msg);
}

template <typename Fn>
auto try_status(Fn&& fn) -> Status {
    try {
        std::forward<Fn>(fn)();
        return {};
    } catch (const std::exception& e) {
        return std::unexpected(std::string(e.what()));
    } catch (...) {
        return std::unexpected("Unknown error");
    }
}

}  // namespace util

#define TRY(expr)                                                              \
    ({                                                                         \
        auto&& __try_result = (expr);                                          \
        using __result_type = std::decay_t<decltype(__try_result)>;            \
        using __value_type = typename __result_type::value_type;               \
        static_assert(!std::is_void_v<__value_type>,                           \
                      "TRY requires std::expected<T, E> where T is not void. " \
                      "Use TRY_void for std::expected<void, E>.");             \
        if (!__try_result) {                                                   \
            return std::unexpected(std::move(__try_result.error()));           \
        }                                                                      \
        std::move(*__try_result);                                              \
    })

#define TRY_void(expr)                                                         \
    do {                                                                       \
        auto&& __try_result = (expr);                                          \
        using __result_type = std::decay_t<decltype(__try_result)>;            \
        using __value_type = typename __result_type::value_type;               \
        static_assert(std::is_void_v<__value_type>,                            \
                      "TRY_void requires std::expected<void, E>. "             \
                      "Use TRY for std::expected<T, E> where T is not void."); \
        if (!__try_result) {                                                   \
            return std::unexpected(std::move(__try_result.error()));           \
        }                                                                      \
    } while (0)

