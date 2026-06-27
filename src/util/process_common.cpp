#include "util/process_detail.hpp"

namespace util::process::detail {

auto cancellation_requested(const RunOptions& options) -> bool {
    return options.cancel_requested != nullptr &&
           options.cancel_requested->load();
}

auto timeout_expired(std::chrono::steady_clock::time_point wall_start,
                     const RunOptions& options) -> bool {
    if (!options.timeout_ms.has_value() || *options.timeout_ms <= 0) {
        return false;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - wall_start);
    return elapsed.count() >= *options.timeout_ms;
}

}  // namespace util::process::detail
