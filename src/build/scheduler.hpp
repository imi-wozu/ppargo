#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <span>

#include "build/compile_profile.hpp"

namespace build::scheduler {

struct SignatureContext {
    std::string signature_hash;
    std::string mode_tag;
};

struct WeightedPath {
    std::filesystem::path path;
    double weight_ms = 1.0;
    std::int64_t recency_rank = 0;
};

auto fallback_cost_ms(const std::filesystem::path& source) -> double;
auto recency_rank_for_source(const std::filesystem::path& source) -> std::int64_t;

auto task_key_for_source(const std::filesystem::path& source,
                         const SignatureContext& context) -> std::string;

auto order_tasks_by_cost(std::vector<WeightedPath> tasks)
    -> std::vector<WeightedPath>;

auto make_balanced_batches(std::span<const WeightedPath>  tasks,
                           std::size_t batch_count)
    -> std::vector<std::vector<std::filesystem::path>>;

}  // namespace build::scheduler
