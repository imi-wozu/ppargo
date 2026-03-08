#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace util {

enum class ErrorKind {
    Validation,
    Parse,
    Io,
    Process,
    Toolchain,
    Manifest,
    Lockfile,
    Build,
    Package,
    Registry,
    Cli,
    Internal,
};

struct Error {
    ErrorKind kind{ErrorKind::Internal};
    std::string message;

    Error() = default;

    Error(ErrorKind kind_value, std::string message_value)
        : kind(kind_value), message(std::move(message_value)) {}

    auto empty() const -> bool { return message.empty(); }
    auto c_str() const -> const char* { return message.c_str(); }
    auto find(std::string_view value, std::size_t pos = 0) const
        -> std::size_t {
        return message.find(value, pos);
    }

    operator std::string_view() const noexcept { return message; }
};

inline auto make_error(ErrorKind kind, std::string message) -> Error {
    return Error{kind, std::move(message)};
}

inline auto validation_error(std::string message) -> Error {
    return make_error(ErrorKind::Validation, std::move(message));
}

inline auto parse_error(std::string message) -> Error {
    return make_error(ErrorKind::Parse, std::move(message));
}

inline auto io_error(std::string message) -> Error {
    return make_error(ErrorKind::Io, std::move(message));
}

inline auto process_error(std::string message) -> Error {
    return make_error(ErrorKind::Process, std::move(message));
}

inline auto toolchain_error(std::string message) -> Error {
    return make_error(ErrorKind::Toolchain, std::move(message));
}

inline auto manifest_error(std::string message) -> Error {
    return make_error(ErrorKind::Manifest, std::move(message));
}

inline auto lockfile_error(std::string message) -> Error {
    return make_error(ErrorKind::Lockfile, std::move(message));
}

inline auto build_error(std::string message) -> Error {
    return make_error(ErrorKind::Build, std::move(message));
}

inline auto package_error(std::string message) -> Error {
    return make_error(ErrorKind::Package, std::move(message));
}

inline auto registry_error(std::string message) -> Error {
    return make_error(ErrorKind::Registry, std::move(message));
}

inline auto cli_error(std::string message) -> Error {
    return make_error(ErrorKind::Cli, std::move(message));
}

inline auto internal_error(std::string message) -> Error {
    return make_error(ErrorKind::Internal, std::move(message));
}

}  // namespace util
