#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "util/result.hpp"

namespace registry::reference::detail {

struct Credential {
    std::string token;
};

auto load_credentials() -> util::Result<std::map<std::string, Credential>>;
auto save_credentials(const std::map<std::string, Credential>& credentials)
    -> util::Status;
auto normalize_token(std::string_view  token) -> Credential;
auto require_token(std::string_view  registry) -> util::Result<std::string>;
auto authorization_headers(std::string_view  registry)
    -> util::Result<std::vector<std::string>>;

}  // namespace registry::reference::detail
