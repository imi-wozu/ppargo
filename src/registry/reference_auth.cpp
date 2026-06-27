#include "registry/reference_auth.hpp"

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "package/home.hpp"
#include "registry/reference_support.hpp"
#include "util/fs.hpp"
#include "util/text.hpp"

namespace registry::reference::detail {

namespace {

auto credentials_path() -> util::Result<std::filesystem::path> {
    return package::home::credentials_file();
}

auto parse_toml_assignment(std::string_view  line)
    -> std::optional<std::pair<std::string, std::string>> {
    const auto equals = line.find('=');
    if (equals == std::string::npos) {
        return std::nullopt;
    }

    auto key = util::text::trim_ascii_copy(line.substr(0, equals));
    auto value = util::text::trim_ascii_copy(line.substr(equals + 1));
    if (key.empty() || value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return std::nullopt;
    }

    value = value.substr(1, value.size() - 2);
    std::string unescaped;
    unescaped.reserve(value.size());
    bool escaping = false;
    for (const auto ch : value) {
        if (escaping) {
            unescaped.push_back(ch);
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        unescaped.push_back(ch);
    }
    return std::pair<std::string, std::string>{std::move(key), std::move(unescaped)};
}

auto quote_toml(std::string_view  value) -> std::string {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    for (const auto ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return '"' + escaped + '"';
}

}  // namespace

auto load_credentials() -> util::Result<std::map<std::string, Credential>> {
    const auto file = GUARD(credentials_path());
    std::map<std::string, Credential> credentials;

    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        return credentials;
    }
    if (ec) {
        return std::unexpected(util::make_error("Failed to access credentials file: " +
                                   file.string() + " (" + ec.message() + ")"));
    }

    std::ifstream input(file);
    if (!input.is_open()) {
        return std::unexpected(util::make_error("Failed to open credentials file: " +
                                   file.string()));
    }

    std::string line;
    bool in_registries = false;
    while (std::getline(input, line)) {
        line = util::text::trim_ascii_copy(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        if (line.front() == '[') {
            in_registries = line == "[registries]";
            continue;
        }

        if (in_registries) {
            const auto assignment = parse_toml_assignment(line);
            if (!assignment.has_value()) {
                continue;
            }
            credentials[assignment->first] = Credential{.token = assignment->second};
            continue;
        }

        std::stringstream ss(line);
        std::string registry;
        std::string token;
        std::string ignored_scopes;
        if (std::getline(ss, registry, '|') && std::getline(ss, token, '|')) {
            std::getline(ss, ignored_scopes);
            credentials[util::text::trim_ascii_copy(registry)] =
                normalize_token(util::text::trim_ascii_copy(token));
        }
    }

    return credentials;
}

auto save_credentials(const std::map<std::string, Credential>& credentials)
    -> util::Status {
    const auto file = GUARD(credentials_path());
    std::ostringstream out;
    out << "[registries]\n";
    for (const auto& [registry, credential] : credentials) {
        out << registry << " = " << quote_toml(credential.token) << "\n";
    }
    return util::fs::atomic_write_text_result(file, out.str());
}

auto normalize_token(std::string_view  token) -> Credential {
    const auto delimiter = token.find("::scopes=");
    if (delimiter == std::string::npos) {
        return Credential{.token = std::string(token)};
    }
    return Credential{.token = std::string(token.substr(0, delimiter))};
}

auto require_token(std::string_view  registry) -> util::Result<std::string> {
    const auto credentials = GUARD(load_credentials());
    const auto found = credentials.find(std::string(registry));
    if (found == credentials.end() || found->second.token.empty()) {
        return std::unexpected(util::make_error(std::format(
            "Auth Error: Not logged in for registry '{}'. Run `argo login --registry {}` first.",
            registry, registry)));
    }
    return found->second.token;
}

auto authorization_headers(std::string_view  registry)
    -> util::Result<std::vector<std::string>> {
    const auto token = GUARD(require_token(registry));
    return std::vector<std::string>{"Authorization: Bearer " + token};
}

}  // namespace registry::reference::detail



