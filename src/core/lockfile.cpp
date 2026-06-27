#include "core/lockfile.hpp"

namespace core {

auto make_lock_package_id(std::string_view name, std::string_view version,
                          std::string_view source) -> std::string {
    return std::string(name) + "@" + std::string(version) + " (" +
           std::string(source) + ")";
}

auto lock_package_name_from_id(std::string_view id) -> std::string {
    const auto at = id.find('@');
    if (at == std::string_view::npos) {
        return std::string(id);
    }
    return std::string(id.substr(0, at));
}

}  // namespace core
