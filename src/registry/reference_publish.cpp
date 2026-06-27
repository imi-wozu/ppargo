#include "registry/reference.hpp"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "registry/reference_auth.hpp"
#include "registry/reference_support.hpp"
#include "util/process.hpp"

namespace registry::reference {

namespace {

auto create_package_artifact(const std::filesystem::path& project_root,
                             const core::Manifest& manifest,
                             std::string_view  registry_name)
    -> util::Result<std::filesystem::path> {
    const auto artifact_root =
        project_root / "target" / "registry" / registry_name;

    std::error_code ec;
    std::filesystem::create_directories(artifact_root, ec);
    if (ec) {
        return std::unexpected(util::make_error(std::format(
            "Failed to create package staging directory {} ({})",
            artifact_root.string(), ec.message())));
    }

    const auto artifact = artifact_root /
                          (manifest.package.name + "-" + manifest.package.version + ".crate");
    std::vector<std::string> archive_args{"-czf", artifact.string(),
                                          "-C", project_root.string(),
                                          "ppargo.toml", "src"};

    if (std::filesystem::exists(project_root / "include", ec) && !ec) {
        archive_args.push_back("include");
    }

    util::process::RunOptions options{};
    options.capture_output = true;
    options.merge_stderr = true;
    options.stdin_mode = util::process::StdinMode::Null;
    auto package_result = GUARD(util::process::run_result("tar", archive_args, options));
    if (package_result.exit_code != 0) {
        return std::unexpected(util::make_error("Failed to create package tarball artifact."));
    }

    return artifact;
}

auto request_api_with_auth(const core::Manifest& manifest, std::string_view  registry,
                           std::string_view  method, std::string_view  suffix,
                           const std::optional<std::filesystem::path>& upload_file = std::nullopt)
    -> util::Result<detail::HttpResponse> {
    const auto endpoints = GUARD(detail::resolve_registry_endpoints(manifest, registry));
    const auto headers = GUARD(detail::authorization_headers(endpoints.name));
    return detail::http_request(method, detail::join_url(endpoints.api, suffix), headers,
                                upload_file);
}

auto status_prefix_for_http_code(int status_code) -> std::string {
    if (status_code == 401 || status_code == 403) {
        return "Auth Error";
    }
    return "Registry Error";
}

auto ensure_successful_api_response(const detail::HttpResponse& response,
                                    std::string_view  operation,
                                    int expected_status,
                                    std::string_view fallback_message) -> util::Status {
    if (response.status_code == expected_status) {
        return util::Ok;
    }
    return std::unexpected(util::make_error(std::format(
        "{}: {} failed: {}",
        status_prefix_for_http_code(response.status_code), operation,
        detail::http_error_message(response, fallback_message))));
}

}  // namespace

auto publish_package(const std::filesystem::path& project_root,
                     const core::Manifest& manifest,
                     std::string_view  registry) -> util::Status {
    const auto endpoints = GUARD(detail::resolve_registry_endpoints(manifest, registry));
    const auto artifact = GUARD(create_package_artifact(project_root, manifest, endpoints.name));
    auto response = GUARD(request_api_with_auth(manifest, registry, "PUT",
                              std::format("crates/{}/{}", manifest.package.name,
                                          manifest.package.version),
                              artifact));
    if (response.status_code == 201) {
        return util::Ok;
    }
    if (response.status_code == 409) {
        return std::unexpected(util::make_error(std::format(
            "Package '{}' version '{}' already exists and is immutable.",
            manifest.package.name, manifest.package.version)));
    }
    return ensure_successful_api_response(response, "publish", 201,
                                          "publish failed");
}

auto yank_package(const core::Manifest& manifest, std::string_view  registry,
                  std::string_view  package_name, std::string_view  version,
                  bool undo) -> util::Status {
    auto response = GUARD(request_api_with_auth(manifest, registry, undo ? "DELETE" : "PUT",
                              std::format("crates/{}/{}/yank", package_name, version)));
    if (response.status_code == 404) {
        return std::unexpected(util::make_error(std::format(
            "Version '{}' for package '{}' was not found.",
            version, package_name)));
    }
    return ensure_successful_api_response(response, undo ? "unyank" : "yank", 200,
                                          undo ? "unyank failed" : "yank failed");
}

auto add_owner(const core::Manifest& manifest, std::string_view  registry,
               std::string_view  package_name, std::string_view  owner)
    -> util::Status {
    auto response = GUARD(request_api_with_auth(manifest, registry, "PUT",
                              std::format("crates/{}/owners/{}", package_name, owner)));
    if (response.status_code == 404) {
        return std::unexpected(util::make_error(std::format("Package '{}' was not found.", package_name)));
    }
    return ensure_successful_api_response(response, "owner add", 200,
                                          "owner add failed");
}

auto remove_owner(const core::Manifest& manifest, std::string_view  registry,
                  std::string_view  package_name, std::string_view  owner)
    -> util::Status {
    auto response = GUARD(request_api_with_auth(manifest, registry, "DELETE",
                              std::format("crates/{}/owners/{}", package_name, owner)));
    if (response.status_code == 404) {
        return std::unexpected(util::make_error(std::format("Package '{}' was not found.", package_name)));
    }
    return ensure_successful_api_response(response, "owner remove", 200,
                                          "owner remove failed");
}

auto login(std::string_view  registry, std::string_view  token) -> util::Status {
    auto credentials = GUARD(detail::load_credentials());
    credentials[std::string(registry)] = detail::normalize_token(token);
    return detail::save_credentials(credentials);
}

auto logout(std::string_view  registry) -> util::Status {
    auto credentials = GUARD(detail::load_credentials());
    credentials.erase(std::string(registry));
    return detail::save_credentials(credentials);
}

}  // namespace registry::reference



