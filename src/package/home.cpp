#include "package/home.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "util/environment.hpp"
#include "util/result.hpp"
#include "util/text.hpp"

namespace {

auto io_error(std::string_view action, const std::filesystem::path& path,
              const std::error_code& error) -> util::Status {
    return std::unexpected(util::make_error(std::format(
        "Failed to {}: {} ({})", action, path.string(), error.message())));
}

auto create_dir(const std::filesystem::path& path) -> util::Status {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        return io_error("create directory", path, error);
    }
    return util::Ok;
}

auto trimmed_env(std::string_view name) -> std::optional<std::string> {
    if (const auto value = util::env::get(name); value.has_value()) {
        auto normalized = util::text::trim_ascii_copy(*value);
        if (!normalized.empty()) {
            return normalized;
        }
    }
    return std::nullopt;
}

#ifdef _WIN32

auto resolve_windows_home() -> util::Result<std::filesystem::path> {
    if (const auto configured = trimmed_env("PPARGO_HOME");
        configured.has_value()) {
        return std::filesystem::path(*configured);
    }

    const auto user_profile = trimmed_env("USERPROFILE");
    if (!user_profile.has_value()) {
        return std::unexpected(
            util::make_error("Environment Error: USERPROFILE is not set. Set "
                             "PPARGO_HOME or USERPROFILE."));
    }

    return std::filesystem::path(*user_profile) / ".ppargo";
}

#endif

}  // namespace

namespace package::home {

auto root() -> util::Result<std::filesystem::path> {
#ifdef _WIN32
    return resolve_windows_home();
#else
    const auto home = trimmed_env("HOME");
    if (!home.has_value()) {
        return std::unexpected(
            util::make_error("Environment Error: HOME is not set."));
    }
    return std::filesystem::path(*home) / ".ppargo";
#endif
}

auto ensure_layout() -> util::Result<std::filesystem::path> {
    auto base = GUARD(root());
    GUARD(create_dir(base));
    GUARD(create_dir(base / "registry" / "index"));
    GUARD(create_dir(base / "registry" / "cache"));
    GUARD(create_dir(base / "registry" / "src"));
    GUARD(create_dir(base / "git" / "checkouts"));
    GUARD(create_dir(base / "git" / "db"));
    return base;
}

auto registry_index_dir() -> util::Result<std::filesystem::path> {
    auto base = GUARD(ensure_layout());
    return base / "registry" / "index";
}

auto registry_cache_dir() -> util::Result<std::filesystem::path> {
    auto base = GUARD(ensure_layout());
    return base / "registry" / "cache";
}

auto registry_src_dir() -> util::Result<std::filesystem::path> {
    auto base = GUARD(ensure_layout());
    return base / "registry" / "src";
}

auto git_checkout_dir() -> util::Result<std::filesystem::path> {
    auto base = GUARD(ensure_layout());
    return base / "git" / "checkouts";
}

auto credentials_file() -> util::Result<std::filesystem::path> {
    auto base = GUARD(ensure_layout());
    return base / "credentials.toml";
}

}  // namespace package::home
