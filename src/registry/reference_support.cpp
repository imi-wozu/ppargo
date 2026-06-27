#include "registry/reference_support.hpp"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#include "package/home.hpp"
#include "util/fs.hpp"
#include "util/text.hpp"

namespace registry::reference::detail {

namespace {

constexpr auto kDefaultIndex = "sparse+http://127.0.0.1:8080/index/";
constexpr auto kDefaultApi = "http://127.0.0.1:8080/api/v1";

auto normalize_index_url(std::string_view value) -> std::string {
    std::string normalized = util::text::trim_ascii_copy(value);
    if (normalized.rfind("sparse+", 0) == 0) {
        normalized.erase(0, 7);
    }
    while (!normalized.empty() && normalized.back() == '/') {
        normalized.pop_back();
    }
    if (normalized.ends_with("/index")) {
        normalized.push_back('/');
        return normalized;
    }
    if (!normalized.empty() && normalized.find("/index/") != std::string::npos) {
        if (normalized.back() != '/') {
            normalized.push_back('/');
        }
        return normalized;
    }
    if (!normalized.empty()) {
        normalized += "/index/";
    }
    return normalized;
}

auto normalize_api_url(std::string_view value) -> std::string {
    std::string normalized = util::text::trim_ascii_copy(value);
    while (!normalized.empty() && normalized.back() == '/') {
        normalized.pop_back();
    }
    if (normalized.empty()) {
        return normalized;
    }
    if (normalized.ends_with("/api/v1")) {
        return normalized;
    }
    return normalized + "/api/v1";
}

auto cached_index_path(std::string_view registry_name,
                       std::string_view package_name)
    -> util::Result<std::filesystem::path> {
    const auto base = GUARD(package::home::registry_index_dir());
    const auto sparse_path = GUARD(sparse_path_for(package_name));
    return base / registry_name / std::filesystem::path(sparse_path);
}

}  // namespace

auto normalize_registry(const core::Manifest& manifest, std::string_view registry)
    -> std::string {
    if (!registry.empty()) {
        return std::string(registry);
    }
    if (manifest.registries.contains("default")) {
        return "default";
    }
    return "ppargo";
}

auto resolve_registry_endpoints(const core::Manifest& manifest,
                                std::string_view registry)
    -> util::Result<RegistryEndpoints> {
    const auto registry_name = normalize_registry(manifest, registry);

    std::string index = kDefaultIndex;
    std::string api = kDefaultApi;

    if (const auto found = manifest.registries.find(registry_name);
        found != manifest.registries.end()) {
        index = found->second.index;
        api = found->second.api;
    } else if (registry_name != "ppargo") {
        return std::unexpected(util::make_error(std::format(
            "Registry '{}' is not configured. Add [registries].{} with index/api endpoints.",
            registry_name, registry_name)));
    }

    index = normalize_index_url(index);
    api = api.empty() ? normalize_api_url(index) : normalize_api_url(api);
    if (index.empty()) {
        return std::unexpected(util::make_error(std::format(
            "Registry '{}' is missing a valid sparse index URL.", registry_name)));
    }
    if (api.empty()) {
        return std::unexpected(util::make_error(std::format(
            "Registry '{}' is missing a valid API URL.", registry_name)));
    }

    return RegistryEndpoints{
        .name = registry_name,
        .index = std::move(index),
        .api = std::move(api),
    };
}

auto sparse_path_for(std::string_view package_name) -> util::Result<std::string> {
    if (package_name.empty()) {
        return std::unexpected(util::make_error("Package name must not be empty."));
    }

    if (package_name.size() == 1) {
        return std::string{"1/"} + std::string(package_name);
    }
    if (package_name.size() == 2) {
        return std::string{"2/"} + std::string(package_name);
    }
    if (package_name.size() == 3) {
        return std::format("3/{}/{}", package_name.substr(0, 1), package_name);
    }
    return std::format("{}/{}/{}", package_name.substr(0, 2),
                       package_name.substr(2, 2), package_name);
}

auto cache_index_body(std::string_view registry_name,
                      std::string_view package_name, std::string_view body)
    -> util::Status {
    const auto path = GUARD(cached_index_path(registry_name, package_name));
    return util::fs::atomic_write_text_result(path, std::string(body));
}

}  // namespace registry::reference::detail
