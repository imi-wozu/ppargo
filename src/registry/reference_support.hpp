#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <span>

#include "core/manifest.hpp"
#include "util/result.hpp"

namespace registry::reference::detail {

struct RegistryEndpoints {
    std::string name;
    std::string index;
    std::string api;
};

struct HttpResponse {
    int status_code = 0;
    std::string body;
};

auto resolve_registry_endpoints(const core::Manifest& manifest,
                                std::string_view  registry)
    -> util::Result<RegistryEndpoints>;
auto sparse_path_for(std::string_view  package_name) -> util::Result<std::string>;
auto cache_index_body(std::string_view  registry_name,
                      std::string_view  package_name,
                      std::string_view  body) -> util::Status;

auto http_request(std::string_view  method, std::string_view  url,
                  std::span<const std::string>  headers = {},
                  const std::optional<std::filesystem::path>& upload_file = std::nullopt)
    -> util::Result<HttpResponse>;
auto http_download(std::string_view  url,
                   const std::filesystem::path& output_file,
                   std::span<const std::string>  headers = {})
    -> util::Result<HttpResponse>;

auto extract_json_string_field(std::string_view json, std::string_view field)
    -> std::optional<std::string>;
auto extract_json_bool_field(std::string_view json, std::string_view field)
    -> std::optional<bool>;
auto extract_json_string_array_field(std::string_view json,
                                     std::string_view field)
    -> std::optional<std::vector<std::string>>;

auto http_error_message(const HttpResponse& response,
                        std::string_view fallback_message) -> std::string;
auto join_url(std::string_view  base, std::string_view  suffix) -> std::string;

auto normalize_registry(const core::Manifest& manifest, std::string_view  registry)
    -> std::string;

}  // namespace registry::reference::detail
