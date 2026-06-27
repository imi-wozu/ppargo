#include "build/action_scheduler.hpp"

#include <algorithm>
#include <condition_variable>
#include <format>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "util/environment.hpp"
#include "util/output.hpp"

namespace build::scheduler_runtime {

namespace {

struct NodeState {
    std::size_t pending_dependencies = 0;
    bool running = false;
    bool finished = false;
};

auto compare_ready(const actions::ActionNode& lhs,
                   const actions::ActionNode& rhs) -> bool {
    if (lhs.recency_rank != rhs.recency_rank) {
        return lhs.recency_rank > rhs.recency_rank;
    }
    if (lhs.estimated_ms != rhs.estimated_ms) {
        return lhs.estimated_ms > rhs.estimated_ms;
    }
    if (lhs.kind != rhs.kind) {
        return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
    }
    if (lhs.display_name != rhs.display_name) {
        return lhs.display_name < rhs.display_name;
    }
    return lhs.id.value < rhs.id.value;
}

auto pool_limit_for(const CapacityPlan& plan, actions::PoolKind kind) -> int {
    for (const auto& pool : plan.pools) {
        if (pool.kind == kind) {
            return std::max(1, pool.depth);
        }
    }
    return 1;
}

enum class BlockReason {
    None,
    Pool,
    Memory,
    Slots,
    Console,
};

struct CandidatePick {
    std::optional<actions::ActionId> id;
    BlockReason reason = BlockReason::None;
};

auto validate_graph(const actions::ActionGraph& graph) -> std::optional<util::Error> {
    std::vector<std::size_t> pending_dependencies(graph.nodes.size(), 0);
    std::vector<std::vector<actions::ActionId>> reverse_edges(graph.nodes.size());

    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
        const auto& node = graph.nodes[index];
        if (node.id.value != index) {
            return util::make_error(std::format(
                "Action graph contains mismatched node id {} at index {}.",
                node.id.value, index));
        }

        pending_dependencies[index] = node.deps.size();
        for (const auto& dep : node.deps) {
            if (dep.value >= graph.nodes.size()) {
                return util::make_error(std::format(
                    "Action graph contains invalid dependency id {} for action '{}'.",
                    dep.value, node.display_name));
            }
            reverse_edges[dep.value].push_back(node.id);
        }
    }

    std::vector<actions::ActionId> ready;
    ready.reserve(graph.nodes.size());
    for (const auto& node : graph.nodes) {
        if (pending_dependencies[node.id.value] == 0) {
            ready.push_back(node.id);
        }
    }

    std::size_t visited = 0;
    while (!ready.empty()) {
        const auto id = ready.back();
        ready.pop_back();
        ++visited;

        for (const auto& next : reverse_edges[id.value]) {
            auto& pending = pending_dependencies[next.value];
            if (pending > 0) {
                --pending;
            }
            if (pending == 0) {
                ready.push_back(next);
            }
        }
    }

    if (visited != graph.nodes.size()) {
        return util::make_error(
            "Action graph contains a dependency cycle or unschedulable actions.");
    }

    return std::nullopt;
}

}  // namespace

auto execute_action_graph(const actions::ActionGraph& graph,
                          const CapacityPlan& plan,
                          const SchedulerOptions& options,
                          const ActionExecutor& executor)
    -> SchedulerResult {
    SchedulerResult result{};
    if (graph.nodes.empty()) {
        result.success = true;
        return result;
    }

    if (auto validation_error = validate_graph(graph); validation_error.has_value()) {
        result.first_error = std::move(validation_error);
        return result;
    }

    std::atomic_bool local_cancel{false};
    auto* const cancel_requested =
        options.cancel_requested != nullptr ? options.cancel_requested : &local_cancel;

    std::vector<NodeState> state(graph.nodes.size());
    std::vector<std::vector<actions::ActionId>> reverse_edges(graph.nodes.size());
    for (const auto& node : graph.nodes) {
        state[node.id.value].pending_dependencies = node.deps.size();
        for (const auto& dep : node.deps) {
            reverse_edges[dep.value].push_back(node.id);
        }
    }

    std::vector<actions::ActionId> ready;
    ready.reserve(graph.nodes.size());
    for (const auto& node : graph.nodes) {
        if (state[node.id.value].pending_dependencies == 0) {
            ready.push_back(node.id);
        }
    }
    std::stable_sort(ready.begin(), ready.end(), [&](actions::ActionId lhs,
                                                     actions::ActionId rhs) {
        return compare_ready(graph.nodes[lhs.value], graph.nodes[rhs.value]);
    });

    std::mutex mutex;
    std::condition_variable cv;
    std::unordered_map<actions::PoolKind, int> active_pools;
    std::size_t active_slots = 0;
    std::uint64_t active_memory_mb = 0;
    std::size_t finished_count = 0;
    bool stop_admission = false;
    std::optional<util::Error> first_error;
    std::string last_block_trace;
    const bool trace = util::env::exists("PPARGO_TRACE");

    auto queue_ready = [&](actions::ActionId id) {
        ready.push_back(id);
        std::stable_sort(ready.begin(), ready.end(),
                         [&](actions::ActionId lhs, actions::ActionId rhs) {
                             return compare_ready(graph.nodes[lhs.value],
                                                  graph.nodes[rhs.value]);
                         });
        if (trace) {
            const auto& node = graph.nodes[id.value];
            util::output::trace(std::format(
                "scheduler: enqueue {} pool={} recent={} est={}ms mem={}MB",
                node.display_name, actions::pool_name(node.pool),
                node.recency_rank, node.estimated_ms, node.estimated_peak_mb));
        }
    };

    auto pick_next = [&]() -> CandidatePick {
        CandidatePick pick{};
        for (auto it = ready.begin(); it != ready.end(); ++it) {
            const auto& node = graph.nodes[it->value];
            if (active_slots >=
                static_cast<std::size_t>(std::max(1, plan.total_slots))) {
                pick.reason = BlockReason::Slots;
                continue;
            }
            if (node.requires_console &&
                active_pools[actions::PoolKind::Console] >=
                    pool_limit_for(plan, actions::PoolKind::Console)) {
                pick.reason = BlockReason::Console;
                continue;
            }
            if (active_pools[node.pool] >= pool_limit_for(plan, node.pool)) {
                pick.reason = BlockReason::Pool;
                continue;
            }
            if (active_memory_mb + node.estimated_peak_mb > plan.memory_budget_mb &&
                !(active_slots == 0 &&
                  node.estimated_peak_mb >= plan.memory_budget_mb)) {
                pick.reason = BlockReason::Memory;
                continue;
            }

            pick.id = *it;
            ready.erase(it);
            pick.reason = BlockReason::None;
            return pick;
        }
        return pick;
    };

    auto maybe_trace_block = [&](BlockReason reason) {
        if (!trace || reason == BlockReason::None) {
            return;
        }
        std::string message;
        switch (reason) {
            case BlockReason::Pool:
                message = "scheduler: blocked-on-pool";
                break;
            case BlockReason::Memory:
                message = "scheduler: blocked-on-memory";
                break;
            case BlockReason::Slots:
                message = "scheduler: blocked-on-slots";
                break;
            case BlockReason::Console:
                message = "scheduler: blocked-on-pool console";
                break;
            case BlockReason::None:
                break;
        }
        if (message != last_block_trace) {
            util::output::trace(message);
            last_block_trace = std::move(message);
        }
    };

    const auto worker_count = std::max<std::size_t>(
        1, std::min<std::size_t>(graph.nodes.size(), std::max(1, plan.total_slots)));

    auto worker = [&]() {
        while (true) {
            actions::ActionId picked{};
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&] {
                    if (finished_count == graph.nodes.size()) {
                        return true;
                    }
                    if (stop_admission) {
                        return active_slots == 0;
                    }
                    return !ready.empty();
                });

                if (finished_count == graph.nodes.size() ||
                    (stop_admission && active_slots == 0)) {
                    return;
                }

                while (true) {
                    const auto candidate = pick_next();
                    if (candidate.id.has_value()) {
                        picked = *candidate.id;
                        auto& node_state = state[picked.value];
                        node_state.running = true;
                        ++active_slots;
                        active_memory_mb += graph.nodes[picked.value].estimated_peak_mb;
                        ++active_pools[graph.nodes[picked.value].pool];
                        if (graph.nodes[picked.value].requires_console) {
                            ++active_pools[actions::PoolKind::Console];
                        }
                        last_block_trace.clear();
                        if (trace) {
                            const auto& node = graph.nodes[picked.value];
                            util::output::trace(std::format(
                                "scheduler: dispatch {} pool={} slots={}/{} mem={}MB/{}MB cancel={}",
                                node.display_name, actions::pool_name(node.pool),
                                active_slots, std::max(1, plan.total_slots),
                                active_memory_mb, plan.memory_budget_mb,
                                cancel_requested->load() ? "true" : "false"));
                        }
                        break;
                    }

                    if (stop_admission) {
                        cv.wait(lock, [&] {
                            return finished_count == graph.nodes.size() ||
                                   active_slots == 0;
                        });
                        return;
                    }
                    maybe_trace_block(candidate.reason);
                    cv.wait(lock);
                    if (finished_count == graph.nodes.size() || stop_admission) {
                        return;
                    }
                }
            }

            if (options.observer) {
                options.observer(ActionEvent{
                    .kind = ActionEventKind::Started,
                    .node = &graph.nodes[picked.value],
                });
            }

            const auto status = executor(graph.nodes[picked.value], cancel_requested);

            {
                std::lock_guard<std::mutex> lock(mutex);
                auto& node_state = state[picked.value];
                node_state.running = false;
                node_state.finished = true;
                ++finished_count;

                --active_slots;
                active_memory_mb -= graph.nodes[picked.value].estimated_peak_mb;
                --active_pools[graph.nodes[picked.value].pool];
                if (graph.nodes[picked.value].requires_console) {
                    --active_pools[actions::PoolKind::Console];
                }

                if (!status) {
                    if (!first_error.has_value()) {
                        first_error = status.error();
                        if (options.stop_on_first_error) {
                            stop_admission = true;
                            if (options.cancel_running_actions_on_first_error) {
                                cancel_requested->store(true);
                            }
                        }
                    }
                } else if (!stop_admission) {
                    for (const auto& next : reverse_edges[picked.value]) {
                        auto& next_state = state[next.value];
                        if (next_state.pending_dependencies > 0) {
                            --next_state.pending_dependencies;
                        }
                        if (next_state.pending_dependencies == 0 &&
                            !next_state.running && !next_state.finished) {
                            queue_ready(next);
                        }
                    }
                }

                if (trace) {
                    const auto& node = graph.nodes[picked.value];
                    util::output::trace(std::format(
                        "scheduler: complete {} success={} active_slots={} active_mem={}MB cancel={}",
                        node.display_name, status ? "true" : "false", active_slots,
                        active_memory_mb,
                        cancel_requested->load() ? "true" : "false"));
                }
                cv.notify_all();
            }

            if (options.observer) {
                options.observer(ActionEvent{
                    .kind = ActionEventKind::Finished,
                    .node = &graph.nodes[picked.value],
                });
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) {
        workers.emplace_back(worker);
    }
    for (auto& worker_thread : workers) {
        worker_thread.join();
    }

    result.first_error = std::move(first_error);
    result.success = !result.first_error.has_value();
    return result;
}

}  // namespace build::scheduler_runtime

