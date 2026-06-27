#include "build/compile_progress.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "util/output.hpp"

namespace {

auto count_progress_actions(
    const build::actions::ActionGraph& graph,
    const std::function<bool(const build::actions::ActionNode&)>& include_action)
    -> std::size_t {
    return static_cast<std::size_t>(std::count_if(
        graph.nodes.begin(), graph.nodes.end(), include_action));
}

auto summarize_active_items(const std::set<std::string>& active_items) -> std::string {
    if (active_items.empty()) {
        return {};
    }

    constexpr std::size_t kMaxItems = 6;
    std::string summary;
    std::size_t index = 0;
    for (const auto& item : active_items) {
        if (index == kMaxItems) {
            summary += ", ...";
            break;
        }
        if (!summary.empty()) {
            summary += ", ";
        }
        summary += item;
        ++index;
    }
    return summary;
}

}  // namespace

namespace build::compile::detail {

auto make_progress_observer(
    std::string phase, const build::actions::ActionGraph& graph,
    std::function<bool(const build::actions::ActionNode&)> include_action)
    -> build::scheduler_runtime::ActionObserver {
    if (!util::output::progress_supported()) {
        return {};
    }

    const auto total = count_progress_actions(graph, include_action);
    if (total == 0) {
        return {};
    }

    struct ObserverState {
        std::mutex mutex;
        std::size_t completed = 0;
        bool started = false;
        std::set<std::string> active_items;
    };

    auto state = std::make_shared<ObserverState>();
    return [phase = std::move(phase), total,
            include_action = std::move(include_action),
            state = std::move(state)](
               const build::scheduler_runtime::ActionEvent& event) {
        if (event.node == nullptr || !include_action(*event.node)) {
            return;
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        if (event.kind == build::scheduler_runtime::ActionEventKind::Started) {
            state->active_items.insert(event.node->display_name);
            const auto progress = util::output::ProgressState{
                .phase = phase,
                .completed = state->completed,
                .total = total,
                .current = summarize_active_items(state->active_items),
            };
            if (!state->started) {
                state->started = true;
                util::output::progress_begin(progress);
            } else {
                util::output::progress_update(progress);
            }
            return;
        }

        state->active_items.erase(event.node->display_name);
        ++state->completed;
        util::output::progress_update(util::output::ProgressState{
            .phase = phase,
            .completed = state->completed,
            .total = total,
            .current = summarize_active_items(state->active_items),
        });
        if (state->completed >= total) {
            util::output::progress_finish();
        }
    };
}

}  // namespace build::compile::detail
