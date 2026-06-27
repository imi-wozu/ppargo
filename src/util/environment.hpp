#pragma once

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace util::env {

namespace detail {

struct FreeDeleter {
    template <typename Pointer>
    void operator()(Pointer* pointer) const noexcept {
        std::free(pointer);
    }
};

template <typename Char>
using MallocPtr = std::unique_ptr<Char, FreeDeleter>;

#ifdef _WIN32
inline auto widen_ascii(std::string_view value) -> std::wstring {
    return std::wstring(value.begin(), value.end());
}
#endif

}  // namespace detail

inline auto get(std::string_view name) -> std::optional<std::string> {
#ifdef _WIN32
    char* raw = nullptr;
    std::size_t raw_length = 0;
    const std::string key(name);
    if (_dupenv_s(&raw, &raw_length, key.c_str()) != 0 || raw == nullptr ||
        raw[0] == '\0') {
        std::free(raw);
        return std::nullopt;
    }

    detail::MallocPtr<char> value(raw);
    return std::string(value.get());
#else
    const std::string key(name);
    const char* value = std::getenv(key.c_str());
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

inline auto exists(std::string_view name) -> bool {
    return get(name).has_value();
}

inline auto get_path(std::string_view name)
    -> std::optional<std::filesystem::path> {
#ifdef _WIN32
    wchar_t* raw = nullptr;
    std::size_t raw_length = 0;
    const std::wstring key = detail::widen_ascii(name);
    if (_wdupenv_s(&raw, &raw_length, key.c_str()) != 0 || raw == nullptr ||
        raw[0] == L'\0') {
        std::free(raw);
        return std::nullopt;
    }

    detail::MallocPtr<wchar_t> value(raw);
    return std::filesystem::path(value.get());
#else
    if (const auto value = get(name); value.has_value()) {
        return std::filesystem::path(*value);
    }
    return std::nullopt;
#endif
}

inline auto set(std::string_view name, std::string_view value) -> bool {
#ifdef _WIN32
    const std::string key(name);
    const std::string data(value);
    return _putenv_s(key.c_str(), data.c_str()) == 0;
#else
    const std::string key(name);
    const std::string data(value);
    return setenv(key.c_str(), data.c_str(), 1) == 0;
#endif
}

inline auto unset(std::string_view name) -> bool {
#ifdef _WIN32
    const std::string key(name);
    return _putenv_s(key.c_str(), "") == 0;
#else
    const std::string key(name);
    return unsetenv(key.c_str()) == 0;
#endif
}

class ScopedOverride {
   public:
    ScopedOverride(std::string_view name, std::optional<std::string_view> value)
        : name_(name), original_(get(name)) {
        if (value.has_value()) {
            set(name_, *value);
        } else {
            unset(name_);
        }
    }

    ScopedOverride(const ScopedOverride&) = delete;
    auto operator=(const ScopedOverride&) -> ScopedOverride& = delete;

    ScopedOverride(ScopedOverride&& other) noexcept
        : name_(std::move(other.name_)),
          original_(std::move(other.original_)),
          active_(other.active_) {
        other.active_ = false;
    }

    auto operator=(ScopedOverride&& other) noexcept -> ScopedOverride& {
        if (this == &other) {
            return *this;
        }
        restore();
        name_ = std::move(other.name_);
        original_ = std::move(other.original_);
        active_ = other.active_;
        other.active_ = false;
        return *this;
    }

    ~ScopedOverride() { restore(); }

   private:
    void restore() {
        if (!active_) {
            return;
        }
        if (original_.has_value()) {
            set(name_, *original_);
        } else {
            unset(name_);
        }
        active_ = false;
    }

    std::string name_;
    std::optional<std::string> original_;
    bool active_ = true;
};

}  // namespace util::env
