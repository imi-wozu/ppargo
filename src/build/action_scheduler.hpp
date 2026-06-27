#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "build/action_graph.hpp"
#include "util/result.hpp"

namespace build::scheduler_runtime {

struct PoolConfig {
    actions::PoolKind kind = actions::PoolKind::Compile;
    int depth = 1;
};

struct CapacityPlan {
    int total_slots = 1;
    std::uint64_t memory_budget_mb = 0;
    std::uint64_t reserve_mb = 0;
    std::vector<PoolConfig> pools;
    std::string reason;
};

enum class ActionEventKind {
    Started,
    Finished,
};

struct ActionEvent {
    ActionEventKind kind = ActionEventKind::Started;
    const actions::ActionNode* node = nullptr;
};

using ActionObserver = std::function<void(const ActionEvent&)>;

struct SchedulerOptions {
    bool stop_on_first_error = true;
    bool cancel_running_actions_on_first_error = true;
    std::atomic_bool* cancel_requested = nullptr;
    ActionObserver observer;
};

struct SchedulerResult {
    bool success = false;
    std::optional<util::Error> first_error;
};

using ActionExecutor =
    std::function<util::Status(const actions::ActionNode&, const std::atomic_bool*)>;

auto execute_action_graph(const actions::ActionGraph& graph,
                          const CapacityPlan& plan,
                          const SchedulerOptions& options,
                          const ActionExecutor& executor)
    -> SchedulerResult;

}  // namespace build::scheduler_runtime
