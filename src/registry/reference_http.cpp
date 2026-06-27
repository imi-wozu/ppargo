#include "registry/reference_support.hpp"

#include <charconv>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "util/fs.hpp"
#include "util/process.hpp"
#include "util/text.hpp"

namespace registry::reference::detail {

namespace {

constexpr auto kStatusMarker = "PPARGO_HTTP_STATUS=";

auto parse_http_response(const util::process::RunResult& run)
    -> util::Result<HttpResponse> {
    const auto marker = run.output.rfind(kStatusMarker);
    if (marker == std::string::npos) {
        return std::unexpected(
            util::make_error("Failed to parse HTTP status from curl output."));
    }

    const auto body_end =
        marker > 0 && run.output[marker - 1] == '\n' ? marker - 1 : marker;
    std::string body = run.output.substr(0, body_end);
    if (!body.empty() && body.back() == '\r') {
        body.pop_back();
    }

    int status_code = 0;
    const auto status_text =
        run.output.substr(marker + std::strlen(kStatusMarker));
    const auto begin = status_text.data();
    const auto end = status_text.data() + status_text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, status_code);
    if (ec != std::errc{} || ptr != end) {
        return std::unexpected(util::make_error(
            "Failed to parse HTTP status code from curl output."));
    }

    return HttpResponse{.status_code = status_code, .body = std::move(body)};
}

auto load_failure_body(const std::filesystem::path& path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

}  // namespace

auto http_request(std::string_view method, std::string_view url,
                  std::span<const std::string> headers,
                  const std::optional<std::filesystem::path>& upload_file)
    -> util::Result<HttpResponse> {
    std::vector<std::string> args{"-sS", "-L", "-X", std::string(method)};
    for (const auto& header : headers) {
        args.push_back("-H");
        args.push_back(header);
    }
    if (upload_file.has_value()) {
        args.push_back("--data-binary");
        args.push_back("@" + upload_file->string());
    }
    args.push_back("-o");
    args.push_back("-");
    args.push_back("-w");
    args.push_back(std::string{"\\n"} + kStatusMarker + "%{http_code}");
    args.push_back(std::string(url));

    util::process::RunOptions options{};
    options.capture_output = true;
    options.merge_stderr = true;
    options.stdin_mode = util::process::StdinMode::Null;
    auto run = GUARD(util::process::run_result("curl.exe", args, options));
    if (run.exit_code != 0) {
        return std::unexpected(util::make_error(std::format(
            "curl.exe failed for {} {} ({})", method, url,
            util::text::trim_ascii_copy(run.output))));
    }
    return parse_http_response(run);
}

auto http_download(std::string_view url,
                   const std::filesystem::path& output_file,
                   std::span<const std::string> headers)
    -> util::Result<HttpResponse> {
    std::error_code ec;
    if (output_file.has_parent_path()) {
        std::filesystem::create_directories(output_file.parent_path(), ec);
        if (ec) {
            return std::unexpected(util::make_error(std::format(
                "Failed to create download directory {} ({})",
                output_file.parent_path().string(), ec.message())));
        }
    }

    const auto staged_output =
        util::fs::temporary_path_for(output_file, "download");
    std::vector<std::string> args{"-sS", "-L"};
    for (const auto& header : headers) {
        args.push_back("-H");
        args.push_back(header);
    }
    args.push_back("-o");
    args.push_back(staged_output.string());
    args.push_back("-w");
    args.push_back(kStatusMarker + std::string{"%{http_code}"});
    args.push_back(std::string(url));

    util::process::RunOptions options{};
    options.capture_output = true;
    options.merge_stderr = true;
    options.stdin_mode = util::process::StdinMode::Null;
    auto run = GUARD(util::process::run_result("curl.exe", args, options));
    if (run.exit_code != 0) {
        std::filesystem::remove(staged_output, ec);
        return std::unexpected(util::make_error(std::format(
            "curl.exe failed for GET {} ({})", url,
            util::text::trim_ascii_copy(run.output))));
    }

    auto response = parse_http_response(run);
    if (!response) {
        std::filesystem::remove(staged_output, ec);
        return std::unexpected(std::move(response.error()));
    }
    if (response->status_code < 200 || response->status_code >= 300) {
        response->body = load_failure_body(staged_output);
        std::filesystem::remove(staged_output, ec);
        return *response;
    }

    GUARD(util::fs::publish_staged_file(staged_output, output_file));
    return *response;
}

auto http_error_message(const HttpResponse& response,
                        std::string_view fallback_message) -> std::string {
    if (const auto parsed = extract_json_string_field(response.body, "error")) {
        return *parsed;
    }

    const auto body = util::text::trim_ascii_copy(response.body);
    if (!body.empty()) {
        return body;
    }
    return std::string{fallback_message};
}

auto join_url(std::string_view base, std::string_view suffix) -> std::string {
    const auto trimmed_base = util::text::trim_ascii_copy(base);
    const auto trimmed_suffix = util::text::trim_ascii_copy(suffix);
    if (trimmed_base.empty()) {
        return trimmed_suffix;
    }
    if (trimmed_suffix.empty()) {
        return trimmed_base;
    }

    const auto base_has_slash = trimmed_base.back() == '/';
    const auto suffix_has_slash = trimmed_suffix.front() == '/';
    if (base_has_slash && suffix_has_slash) {
        return trimmed_base + trimmed_suffix.substr(1);
    }
    if (!base_has_slash && !suffix_has_slash) {
        return trimmed_base + "/" + trimmed_suffix;
    }
    return trimmed_base + trimmed_suffix;
}

}  // namespace registry::reference::detail
