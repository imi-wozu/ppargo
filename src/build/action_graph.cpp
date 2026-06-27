#include "build/action_graph.hpp"

#include <algorithm>
#include <utility>

namespace build::actions {

auto append_action(ActionGraph& graph, ActionKind kind, PoolKind pool,
                   std::vector<ActionId> deps, ActionPayload payload,
                   double estimated_ms, std::uint32_t estimated_peak_mb,
                   std::string display_name, bool requires_console,
                   std::int64_t recency_rank) -> ActionId {
    ActionId id{graph.nodes.size()};
    graph.nodes.push_back(ActionNode{
        .id = id,
        .kind = kind,
        .pool = pool,
        .deps = std::move(deps),
        .payload = std::move(payload),
        .estimated_ms = std::max(1.0, estimated_ms),
        .estimated_peak_mb = std::max<std::uint32_t>(1, estimated_peak_mb),
        .recency_rank = recency_rank,
        .display_name = std::move(display_name),
        .requires_console = requires_console,
    });
    return id;
}

auto pool_name(PoolKind pool) -> std::string_view {
    switch (pool) {
        case PoolKind::Compile:
            return "compile";
        case PoolKind::Check:
            return "check";
        case PoolKind::Link:
            return "link";
        case PoolKind::Console:
            return "console";
    }
    return "unknown";
}

}  // namespace build::actions
