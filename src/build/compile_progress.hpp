#pragma once

#include <functional>
#include <string>

#include "build/action_graph.hpp"
#include "build/action_scheduler.hpp"

namespace build::compile::detail {

auto make_progress_observer(
    std::string phase, const build::actions::ActionGraph& graph,
    std::function<bool(const build::actions::ActionNode&)> include_action)
    -> build::scheduler_runtime::ActionObserver;

}  // namespace build::compile::detail
