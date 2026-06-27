#include "build/scheduler.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <queue>
#include <span>
#include <string>
#include <system_error>
#include <vector>
namespace build::scheduler {

auto fallback_cost_ms(const std::filesystem::path& source) -> double {
    std::error_code ec;
    const auto size_bytes = std::filesystem::file_size(source, ec);
    if (ec) {
        return 1.0;
    }
    const double size_kb = static_cast<double>(size_bytes) / 1024.0;
    return std::max(1.0, size_kb);
}

auto recency_rank_for_source(const std::filesystem::path& source) -> std::int64_t {
    std::error_code ec;
    const auto timestamp = std::filesystem::last_write_time(source, ec);
    if (ec) {
        return 0;
    }
    return static_cast<std::int64_t>(timestamp.time_since_epoch().count());
}

auto task_key_for_source(const std::filesystem::path& source,
                         const SignatureContext& context) -> std::string {
    return build::compile_profile::make_task_key(source, context.signature_hash,
                                                 context.mode_tag);
}

auto order_tasks_by_cost(std::vector<WeightedPath> tasks)
    -> std::vector<WeightedPath> {
    std::stable_sort(tasks.begin(), tasks.end(),
                     [](const WeightedPath& lhs, const WeightedPath& rhs) {
                         if (lhs.recency_rank != rhs.recency_rank) {
                             return lhs.recency_rank > rhs.recency_rank;
                         }
                         if (lhs.weight_ms != rhs.weight_ms) {
                             return lhs.weight_ms > rhs.weight_ms;
                         }
                         return lhs.path.generic_string() < rhs.path.generic_string();
                     });
    return tasks;
}

auto make_balanced_batches(std::span<const WeightedPath>  tasks,
                           std::size_t batch_count)
    -> std::vector<std::vector<std::filesystem::path>> {
    if (tasks.empty()) {
        return {};
    }
    const std::size_t actual_batch_count =
        std::max<std::size_t>(1, std::min(batch_count, tasks.size()));

    struct BatchState {
        std::size_t index = 0;
        double load = 0.0;
    };
    auto cmp = [](const BatchState& lhs, const BatchState& rhs) {
        if (lhs.load != rhs.load) {
            return lhs.load > rhs.load;
        }
        return lhs.index > rhs.index;
    };

    std::priority_queue<BatchState, std::vector<BatchState>, decltype(cmp)> queue(cmp);
    std::vector<std::vector<std::filesystem::path>> batches(actual_batch_count);
    for (std::size_t i = 0; i < actual_batch_count; ++i) {
        queue.push(BatchState{.index = i, .load = 0.0});
    }

    for (const auto& task : tasks) {
        auto lightest = queue.top();
        queue.pop();
        batches[lightest.index].push_back(task.path);
        lightest.load += task.weight_ms;
        queue.push(lightest);
    }

    return batches;
}

}  // namespace build::scheduler



