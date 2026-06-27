#include "build/compile.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <span>

#include "build/action_graph.hpp"
#include "build/action_scheduler.hpp"
#include "build/check_batches.hpp"
#include "build/compile_cache.hpp"
#include "build/compile_jobs.hpp"
#include "build/compile_pch.hpp"
#include "build/compile_profile.hpp"
#include "build/compile_progress.hpp"
#include "build/depscan.hpp"
#include "build/fingerprint.hpp"
#include "build/graph.hpp"
#include "build/process_bridge.hpp"
#include "build/scheduler.hpp"
#include "util/environment.hpp"
#include "util/fs.hpp"
#include "util/output.hpp"

namespace {

constexpr std::uint32_t kDefaultCompilePeakMb = 192;
constexpr std::uint32_t kDefaultCheckPeakMb = 256;

struct TimedSample {
    std::string key;
    double elapsed_ms = 0.0;
    std::optional<double> peak_mb;
    double startup_ms = 0.0;
};

struct CompileTaskPlan {
    std::filesystem::path source;
    std::filesystem::path object_file;
    std::filesystem::path dep_file;
    std::string key;
    double estimated_ms = 1.0;
    std::uint32_t estimated_peak_mb = kDefaultCompilePeakMb;
    long long recency_rank = 0;
};

struct CheckTaskPlan {
    std::filesystem::path source;
    std::string relative_path;
    long long mtime = 0;
    long long recency_rank = 0;
    std::uintmax_t size = 0;
    std::string key;
    double estimated_ms = 1.0;
    std::uint32_t estimated_peak_mb = kDefaultCheckPeakMb;
};

struct CompileCandidatePlan {
    CompileTaskPlan task;
    bool stale = false;
};

struct CheckBatchPlan {
    std::vector<std::filesystem::path> sources;
    std::vector<std::string> profile_keys;
    std::vector<std::string> dep_sources;
    double estimated_ms = 1.0;
    std::uint32_t estimated_peak_mb = kDefaultCheckPeakMb;
    long long recency_rank = 0;
};

struct OptimizationSelection {
    build::compile::OptimizationMode mode =
        build::compile::OptimizationMode::Normal;
    std::string reason;
};

auto env_truthy(std::string_view name) -> bool {
    const auto value = util::env::get(name);
    if (!value.has_value()) {
        return false;
    }

    auto normalized = *value;
    std::ranges::transform(normalized, normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return !(normalized == "0" || normalized == "false" || normalized == "off" ||
             normalized == "no");
}

auto aggressive_mode_disabled() -> bool {
    return env_truthy("PPARGO_INTERNAL_DISABLE_AGGRESSIVE");
}

auto select_optimization_mode(
    std::size_t total_tus, std::size_t stale_tasks,
    const build::settings::ResolvedBuildSettings& settings)
    -> OptimizationSelection {
    if (aggressive_mode_disabled()) {
        return OptimizationSelection{
            .mode = build::compile::OptimizationMode::Normal,
            .reason = "disabled by PPARGO_INTERNAL_DISABLE_AGGRESSIVE",
        };
    }

    if (total_tus >= settings.aggressive_tu_threshold) {
        return OptimizationSelection{
            .mode = build::compile::OptimizationMode::Aggressive,
            .reason = std::format("selected_tus={} >= {}", total_tus,
                                  settings.aggressive_tu_threshold),
        };
    }

    if (stale_tasks >= settings.aggressive_stale_threshold) {
        return OptimizationSelection{
            .mode = build::compile::OptimizationMode::Aggressive,
            .reason = std::format("stale_tasks={} >= {}", stale_tasks,
                                  settings.aggressive_stale_threshold),
        };
    }

    return OptimizationSelection{
        .mode = build::compile::OptimizationMode::Normal,
        .reason = "below aggressive thresholds",
    };
}

auto emit_optimization_trace(build::compile::CompileMode compile_mode,
                             const OptimizationSelection& selection,
                             std::size_t total_tus, std::size_t stale_tasks) -> void {
    if (!build::compile::detail::env_var_present("PPARGO_TRACE")) {
        return;
    }

    const auto mode_text = selection.mode == build::compile::OptimizationMode::Aggressive
                               ? "aggressive"
                               : "normal";
    util::output::trace(std::format(
        "optimizer: stage={} mode={} reason={} total_tus={} stale_tasks={}",
        build::compile::detail::mode_name(compile_mode), mode_text, selection.reason,
        total_tus, stale_tasks));
}

auto progress_item_name(const std::filesystem::path& path) -> std::string {
    if (path.filename().empty()) {
        return path.generic_string();
    }
    return path.filename().generic_string();
}

auto check_batch_display_name(
    std::span<const std::filesystem::path>  sources) -> std::string {
    if (sources.empty()) {
        return "check";
    }

    const auto first = progress_item_name(sources.front());
    if (sources.size() == 1) {
        return first;
    }
    return std::format("{} (+{} more)", first, sources.size() - 1);
}

auto emit_scheduler_line(const build::compile::CompilerConfig& config,
                         std::size_t task_count) -> void {
    const auto line = std::format(
        "scheduler: mode={} tasks={} jobs={} ({})",
        build::compile::detail::mode_name(config.mode), task_count, config.jobs,
        config.jobs_reason);
    if (build::compile::detail::env_var_present("PPARGO_BENCHMARK_JOBS")) {
        util::output::info(line);
    } else if (build::compile::detail::scheduler_trace_enabled()) {
        util::output::trace(line);
    }
}

auto with_runtime_capacity(
    const build::compile::CompilerConfig& config, std::size_t ready_task_count,
    const build::compile_profile::CompileProfile& profile)
    -> build::compile::CompilerConfig {
    auto runtime_config = config;
    runtime_config.capacity_plan = build::compile::make_capacity_plan(
        config.mode, ready_task_count,
        build::compile::detail::parse_forced_jobs_override(), profile);
    runtime_config.jobs = runtime_config.capacity_plan.total_slots;
    runtime_config.jobs_reason = runtime_config.capacity_plan.reason;
    return runtime_config;
}

auto peak_mb_for(const build::compile_profile::CompileProfile& profile,
                 std::string_view  key, std::uint32_t fallback)
    -> std::uint32_t {
    const auto peak = build::compile_profile::lookup_peak_mb(profile, key);
    if (!peak.has_value()) {
        return fallback;
    }
    return static_cast<std::uint32_t>(std::max<double>(1.0, *peak));
}

auto source_key(const std::filesystem::path& source) -> std::string {
    return util::fs::path_key(source);
}

auto dependency_depth_for(const build::depscan::DependencyGraph& graph,
                          const std::filesystem::path& source) -> std::size_t {
    const auto found = graph.depths.find(source_key(source));
    return found == graph.depths.end() ? 0 : found->second;
}

void apply_compile_analysis(std::vector<CompileTaskPlan>& tasks,
                            const build::depscan::AnalysisResult& analysis) {
    std::unordered_map<std::string, const build::depscan::AnalyzedSource*> analyzed;
    analyzed.reserve(analysis.sources.size());
    for (const auto& source : analysis.sources) {
        analyzed.emplace(source_key(source.source), &source);
    }

    for (auto& task : tasks) {
        const auto found = analyzed.find(source_key(task.source));
        if (found == analyzed.end()) {
            continue;
        }
        task.recency_rank = found->second->recency_rank;
    }
}

void apply_check_analysis(std::vector<CheckTaskPlan>& stale,
                          const build::depscan::AnalysisResult& analysis) {
    std::unordered_map<std::string, const build::depscan::AnalyzedSource*> analyzed;
    analyzed.reserve(analysis.sources.size());
    for (const auto& source : analysis.sources) {
        analyzed.emplace(source_key(source.source), &source);
    }

    for (auto& task : stale) {
        const auto found = analyzed.find(source_key(task.source));
        if (found == analyzed.end()) {
            task.recency_rank = task.mtime;
            continue;
        }
        task.recency_rank = std::max(task.mtime, found->second->recency_rank);
    }
}

auto make_check_batch_plans(
    const std::vector<CheckTaskPlan>& stale,
    const build::depscan::DependencyGraph& dep_graph,
    std::size_t target_batches) -> std::vector<CheckBatchPlan> {
    std::unordered_map<std::string, const CheckTaskPlan*> task_lookup;
    task_lookup.reserve(stale.size());
    for (const auto& task : stale) {
        task_lookup.emplace(source_key(task.source), &task);
    }

    std::vector<build::scheduler::WeightedPath> weighted;
    weighted.reserve(stale.size());
    for (const auto& task : stale) {
        weighted.push_back(build::scheduler::WeightedPath{
            .path = task.source,
            .weight_ms = task.estimated_ms,
            .recency_rank = task.recency_rank,
        });
    }
    const auto ordered = build::scheduler::order_tasks_by_cost(std::move(weighted));

    std::vector<CheckBatchPlan> plans;
    if (dep_graph.deps.empty() || dep_graph.had_cycle) {
        const auto batches =
            build::scheduler::make_balanced_batches(ordered, target_batches);
        plans.reserve(batches.size());
        for (const auto& batch : batches) {
            CheckBatchPlan plan;
            plan.sources = batch;
            plan.estimated_ms = 0.0;
            plan.estimated_peak_mb = kDefaultCheckPeakMb;
            for (const auto& source : batch) {
                const auto found = task_lookup.find(source_key(source));
                if (found == task_lookup.end()) {
                    continue;
                }
                const auto* task = found->second;
                plan.profile_keys.push_back(task->key);
                plan.estimated_ms += task->estimated_ms;
                plan.estimated_peak_mb =
                    std::max(plan.estimated_peak_mb, task->estimated_peak_mb);
                plan.recency_rank = std::max(plan.recency_rank, task->recency_rank);
            }
            plans.push_back(std::move(plan));
        }
        return plans;
    }

    std::map<std::size_t, std::vector<build::scheduler::WeightedPath>> layers;
    for (const auto& task : ordered) {
        layers[dependency_depth_for(dep_graph, task.path)].push_back(task);
    }

    for (const auto& [depth, layer] : layers) {
        (void)depth;
        const auto layer_batch_count =
            std::max<std::size_t>(1, std::min<std::size_t>(layer.size(), target_batches));
        const auto layer_batches =
            build::scheduler::make_balanced_batches(layer, layer_batch_count);
        for (const auto& batch : layer_batches) {
            CheckBatchPlan plan;
            plan.sources = batch;
            plan.estimated_ms = 0.0;
            plan.estimated_peak_mb = kDefaultCheckPeakMb;

            std::unordered_set<std::string> dep_sources;
            for (const auto& source : batch) {
                const auto found = task_lookup.find(source_key(source));
                if (found == task_lookup.end()) {
                    continue;
                }
                const auto* task = found->second;
                plan.profile_keys.push_back(task->key);
                plan.estimated_ms += task->estimated_ms;
                plan.estimated_peak_mb =
                    std::max(plan.estimated_peak_mb, task->estimated_peak_mb);
                plan.recency_rank = std::max(plan.recency_rank, task->recency_rank);

                const auto deps = dep_graph.deps.find(source_key(source));
                if (deps == dep_graph.deps.end()) {
                    continue;
                }
                for (const auto& dep_source : deps->second) {
                    dep_sources.insert(dep_source);
                }
            }

            plan.dep_sources.assign(dep_sources.begin(), dep_sources.end());
            std::sort(plan.dep_sources.begin(), plan.dep_sources.end());
            plans.push_back(std::move(plan));
        }
    }

    return plans;
}

auto compile_source(const build::compile::CompilerConfig& config,
                    const std::filesystem::path& source,
                    const std::filesystem::path& object_file,
                    const std::filesystem::path& dep_file,
                    const std::atomic_bool* cancel_requested)
    -> util::Result<util::process::ProcessMetrics> {
    std::vector<std::string> args;
    args.reserve(config.flags.size() + config.include_paths.size() * 2 + 8);
    for (const auto& flag : config.flags) {
        args.push_back(flag);
    }
    for (const auto& include_path : config.include_paths) {
        args.push_back("-I");
        args.push_back(include_path.string());
    }
    if (config.pch_file.has_value()) {
        args.push_back("-include-pch");
        args.push_back(config.pch_file->string());
    }
    args.push_back("-c");
    args.push_back(source.string());
    args.push_back("-o");
    args.push_back(object_file.string());
    args.push_back("-MMD");
    args.push_back("-MF");
    args.push_back(dep_file.string());

    auto run = GUARD(build::process_bridge::run(
        config.compiler, args,
        build::process_bridge::RunOptions{
            .cancel_requested = cancel_requested,
            .capture_policy = build::process_bridge::CapturePolicy::BufferedDiagnostics,
        }));
    if (run.canceled) {
        return util::process::ProcessMetrics{};
    }
    if (run.exit_code != 0) {
        return std::unexpected(util::make_error("Compilation failed for: " +
                                   source.string()));
    }
    return run.metrics.value_or(util::process::ProcessMetrics{});
}

auto execute_graph(
    const build::actions::ActionGraph& graph,
    const build::scheduler_runtime::CapacityPlan& plan,
    const build::scheduler_runtime::ActionExecutor& executor,
    const build::scheduler_runtime::ActionObserver& observer = {})
    -> util::Status {
    std::atomic_bool cancel_requested{false};
    build::scheduler_runtime::SchedulerOptions options{};
    options.cancel_requested = &cancel_requested;
    options.observer = observer;
    const auto result =
        build::scheduler_runtime::execute_action_graph(graph, plan, options, executor);
    if (!result.success) {
        return std::unexpected(result.first_error.value_or(
            util::make_error("Action graph failed.")));
    }
    return util::Ok;
}

}  // namespace

namespace build::compile {

auto compile_signature(const CompilerConfig& config) -> std::string {
    std::ostringstream out;
    out << "mode=" << detail::mode_name(config.mode) << "\n";
    out << config.compiler.generic_string() << "\n";
    out << "modules=" << (config.modules_enabled ? "true" : "false") << "\n";
    out << "module-output=" << config.module_output_dir.generic_string() << "\n";
    out << "aggressive-tu-threshold="
        << config.build_settings.aggressive_tu_threshold << "\n";
    out << "aggressive-stale-threshold="
        << config.build_settings.aggressive_stale_threshold << "\n";
    out << "pch-scan-lines=" << config.build_settings.pch_scan_lines << "\n";
    out << "pch-frequency-threshold="
        << config.build_settings.pch_frequency_threshold << "\n";
    out << "pch-max-headers=" << config.build_settings.pch_max_headers << "\n";
    out << "depscan-timeout-ms=" << config.build_settings.depscan_timeout_ms
        << "\n";
    for (const auto& flag : config.flags) {
        out << flag << "\n";
    }
    out << "--include-paths--\n";
    for (const auto& include_path : config.include_paths) {
        out << include_path.generic_string() << "\n";
    }
    out << "--module-interface-exts--\n";
    for (const auto& ext : config.module_interface_exts) {
        out << ext << "\n";
    }
    return out.str();
}

auto compile_objects(const std::filesystem::path& source_root,
                     const std::filesystem::path& obj_root,
                     std::span<const std::filesystem::path>  sources,
                     const CompilerConfig& config, bool force_rebuild)
    -> util::Result<CompileResult> {
    CompileResult result;
    result.objects.reserve(sources.size());

    const bool trace = detail::incremental_trace_enabled();
    const auto signature_hash = detail::signature_hash_text(compile_signature(config));
    const scheduler::SignatureContext signature_context{
        .signature_hash = signature_hash,
        .mode_tag = std::string(detail::mode_name(CompileMode::Build)),
    };

    const auto build_root = obj_root.parent_path();
    auto profile = GUARD(compile_profile::load_profile(build_root));

    std::vector<CompileCandidatePlan> candidates;
    candidates.reserve(sources.size());
    std::size_t stale_count = 0;

    for (const auto& source : sources) {
        auto object_file =
            GUARD(build::graph::object_path_for_source(source_root, obj_root, source));
        std::error_code ec;
        std::filesystem::create_directories(object_file.parent_path(), ec);
        if (ec) {
            return std::unexpected(util::make_error(
                "Failed to create object directory: " +
                object_file.parent_path().string() + " (" + ec.message() + ")"));
        }

        auto dep_file = object_file;
        dep_file.replace_extension(".d");

        const auto decision =
            force_rebuild
                ? build::fingerprint::RebuildDecision{}
                : build::fingerprint::evaluate_rebuild(source_root, source,
                                                       object_file, dep_file);
        const auto key = scheduler::task_key_for_source(source, signature_context);
        const auto recency_rank = GUARD(detail::file_stamp(source));
        const bool stale =
            force_rebuild ||
            decision.reason != build::fingerprint::RebuildReason::UpToDate;
        candidates.push_back(CompileCandidatePlan{
            .task =
                CompileTaskPlan{
                    .source = source,
                    .object_file = object_file,
                    .dep_file = dep_file,
                    .key = key,
                    .estimated_ms = compile_profile::lookup_cost_ms(profile, key)
                                        .value_or(scheduler::fallback_cost_ms(source)),
                    .estimated_peak_mb = peak_mb_for(profile, key, kDefaultCompilePeakMb),
                    .recency_rank = recency_rank,
                },
            .stale = stale,
        });
        if (stale) {
            ++stale_count;
            if (trace) {
                if (force_rebuild) {
                    util::output::trace(std::format(
                        "incremental: {} [ForceRebuild]", source.string()));
                } else if (decision.dependency.empty()) {
                    util::output::trace(std::format(
                        "incremental: {} [{}]", source.string(),
                        build::fingerprint::to_string(decision.reason)));
                } else {
                    util::output::trace(std::format(
                        "incremental: {} [{}: {}]", source.string(),
                        build::fingerprint::to_string(decision.reason),
                        decision.dependency.string()));
                }
            }
        }

        result.objects.push_back(object_file);
    }

    const auto optimization = select_optimization_mode(
        sources.size(), stale_count, config.build_settings);
    emit_optimization_trace(CompileMode::Build, optimization, sources.size(),
                            stale_count);

    auto analysis_config = with_runtime_capacity(config, stale_count, profile);
    analysis_config.optimization_mode = optimization.mode;
    analysis_config.optimization_reason = optimization.reason;

    std::vector<CompileTaskPlan> tasks;
    tasks.reserve(candidates.size());
    build::depscan::DependencyGraph dep_graph;

    if (!candidates.empty()) {
        if (analysis_config.modules_enabled && stale_count > 0) {
            std::vector<std::filesystem::path> analysis_sources;
            std::vector<std::filesystem::path> analysis_depfiles;
            analysis_sources.reserve(candidates.size());
            analysis_depfiles.reserve(candidates.size());
            for (const auto& candidate : candidates) {
                analysis_sources.push_back(candidate.task.source);
                analysis_depfiles.push_back(candidate.task.dep_file);
            }

            const auto analysis = GUARD(build::depscan::analyze_sources(
                source_root, build_root, analysis_sources, analysis_config,
                analysis_depfiles));
            dep_graph = build::depscan::build_dependency_graph(analysis.sources);
            for (auto& candidate : candidates) {
                std::vector<CompileTaskPlan> one_task{candidate.task};
                apply_compile_analysis(one_task, analysis);
                candidate.task = std::move(one_task.front());
            }

            if (!dep_graph.had_cycle) {
                std::unordered_set<std::string> stale_sources;
                for (const auto& candidate : candidates) {
                    if (candidate.stale) {
                        stale_sources.insert(source_key(candidate.task.source));
                    }
                }

                bool changed = true;
                while (changed) {
                    changed = false;
                    for (auto& candidate : candidates) {
                        if (candidate.stale) {
                            continue;
                        }
                        const auto deps = dep_graph.deps.find(source_key(candidate.task.source));
                        if (deps == dep_graph.deps.end()) {
                            continue;
                        }
                        const bool depends_on_stale = std::ranges::any_of(
                            deps->second, [&](const std::string& dep_source) {
                                return stale_sources.contains(dep_source);
                            });
                        if (!depends_on_stale) {
                            continue;
                        }
                        candidate.stale = true;
                        stale_sources.insert(source_key(candidate.task.source));
                        changed = true;
                    }
                }
            }
        }

        for (const auto& candidate : candidates) {
            if (candidate.stale) {
                tasks.push_back(candidate.task);
            }
        }

        if (!analysis_config.modules_enabled && !tasks.empty()) {
            std::vector<std::filesystem::path> task_sources;
            std::vector<std::filesystem::path> task_depfiles;
            task_sources.reserve(tasks.size());
            task_depfiles.reserve(tasks.size());
            for (const auto& task : tasks) {
                task_sources.push_back(task.source);
                task_depfiles.push_back(task.dep_file);
            }

            const auto analysis = GUARD(build::depscan::analyze_sources(
                source_root, build_root, task_sources, analysis_config, task_depfiles));
            apply_compile_analysis(tasks, analysis);
            dep_graph = build::depscan::build_dependency_graph(analysis.sources);
        }
    }

    std::stable_sort(tasks.begin(), tasks.end(),
                     [&](const CompileTaskPlan& lhs, const CompileTaskPlan& rhs) {
                         const auto lhs_depth = dep_graph.had_cycle
                                                    ? 0U
                                                    : dependency_depth_for(dep_graph,
                                                                           lhs.source);
                         const auto rhs_depth = dep_graph.had_cycle
                                                    ? 0U
                                                    : dependency_depth_for(dep_graph,
                                                                           rhs.source);
                         if (lhs_depth != rhs_depth) {
                             return lhs_depth < rhs_depth;
                         }
                         if (lhs.recency_rank != rhs.recency_rank) {
                             return lhs.recency_rank > rhs.recency_rank;
                         }
                         if (lhs.estimated_ms != rhs.estimated_ms) {
                             return lhs.estimated_ms > rhs.estimated_ms;
                         }
                         return lhs.source.generic_string() < rhs.source.generic_string();
                     });

    auto runtime_config = with_runtime_capacity(config, tasks.size(), profile);
    runtime_config.optimization_mode = optimization.mode;
    runtime_config.optimization_reason = optimization.reason;
    if (runtime_config.optimization_mode == OptimizationMode::Aggressive) {
        runtime_config.pch_file =
            detail::ensure_auto_pch(runtime_config, build_root, sources);
    }

    emit_scheduler_line(runtime_config, tasks.size());
    if (!tasks.empty()) {
        build::actions::ActionGraph graph;
        std::unordered_map<std::string, build::actions::ActionId> action_ids;
        action_ids.reserve(tasks.size());
        for (const auto& task : tasks) {
            std::vector<build::actions::ActionId> deps;
            if (!dep_graph.had_cycle) {
                const auto found = dep_graph.deps.find(source_key(task.source));
                if (found != dep_graph.deps.end()) {
                    for (const auto& dep_source : found->second) {
                        const auto action = action_ids.find(dep_source);
                        if (action != action_ids.end()) {
                            deps.push_back(action->second);
                        }
                    }
                }
            }

            const auto action_id = build::actions::append_action(
                graph, build::actions::ActionKind::Compile,
                build::actions::PoolKind::Compile, std::move(deps),
                build::actions::CompileActionPayload{
                    .source = task.source,
                    .object_file = task.object_file,
                    .dep_file = task.dep_file,
                    .profile_key = task.key,
                },
                task.estimated_ms, task.estimated_peak_mb,
                progress_item_name(task.source), false, task.recency_rank);
            action_ids.emplace(source_key(task.source), action_id);
        }

        const auto observer = detail::make_progress_observer(
            "Compiling", graph, [](const build::actions::ActionNode& node) {
                return node.kind == build::actions::ActionKind::Compile;
            });

        std::vector<TimedSample> timing_samples;
        timing_samples.reserve(tasks.size());
        std::mutex timing_mutex;

        GUARD(execute_graph(
            graph, runtime_config.capacity_plan,
            [&runtime_config, &timing_samples,
             &timing_mutex](const build::actions::ActionNode& node,
                            const std::atomic_bool* cancel_requested) -> util::Status {
                const auto& payload =
                    std::get<build::actions::CompileActionPayload>(node.payload);
                auto metrics = GUARD(compile_source(
                    runtime_config, payload.source, payload.object_file,
                    payload.dep_file, cancel_requested));
                if (metrics.wall_ms > 0.0) {
                    std::lock_guard<std::mutex> lock(timing_mutex);
                    timing_samples.push_back(TimedSample{
                        .key = payload.profile_key,
                        .elapsed_ms = std::max(1.0, metrics.wall_ms),
                        .peak_mb = metrics.peak_working_set_mb,
                        .startup_ms = 0.0,
                    });
                }
                return util::Ok;
            },
            observer));

        for (const auto& sample : timing_samples) {
            compile_profile::update_task_stats(profile, sample.key, sample.elapsed_ms,
                                               sample.peak_mb, sample.startup_ms);
        }
        GUARD(compile_profile::save_profile(build_root, profile));
    }

    result.compiled_count = tasks.size();
    return result;
}

auto run_checks_with_cache(const std::filesystem::path& root,
                           const std::filesystem::path& build_root,
                           std::span<const std::filesystem::path>  sources,
                           const CompilerConfig& config) -> util::Status {
    const auto signature_hash = detail::signature_hash_text(compile_signature(config));
    const auto cache_file = build_root / (".check_cache_" + signature_hash);
    auto cache = detail::load_check_cache(cache_file, signature_hash);
    auto profile = GUARD(compile_profile::load_profile(build_root));

    const scheduler::SignatureContext signature_context{
        .signature_hash = signature_hash,
        .mode_tag = std::string(detail::mode_name(CompileMode::Check)),
    };

    std::vector<CheckTaskPlan> candidates;
    candidates.reserve(sources.size());

    for (const auto& source : sources) {
        std::error_code ec;
        const auto relative_path = std::filesystem::relative(source, root, ec);
        if (ec) {
            return std::unexpected(util::make_error(
                "Failed to resolve relative source path: " +
                source.string()));
        }
        const std::string relative = util::fs::path_key(relative_path);
        auto mtime = GUARD(detail::file_stamp(source));
        auto size = GUARD(detail::file_size_value(source));
        const auto key = scheduler::task_key_for_source(source, signature_context);
        candidates.push_back(CheckTaskPlan{
            .source = source,
            .relative_path = relative,
            .mtime = mtime,
            .recency_rank = mtime,
            .size = size,
            .key = key,
            .estimated_ms = compile_profile::lookup_cost_ms(profile, key)
                                .value_or(scheduler::fallback_cost_ms(source)),
            .estimated_peak_mb = peak_mb_for(profile, key, kDefaultCheckPeakMb),
        });
    }

    auto analysis_config = with_runtime_capacity(config, candidates.size(), profile);
    build::depscan::DependencyGraph dep_graph;
    if (!candidates.empty()) {
        std::vector<std::filesystem::path> analysis_sources;
        analysis_sources.reserve(candidates.size());
        for (const auto& task : candidates) {
            analysis_sources.push_back(task.source);
        }

        const auto analysis = GUARD(build::depscan::analyze_sources(
            root, build_root, analysis_sources, analysis_config));
        apply_check_analysis(candidates, analysis);
        dep_graph = build::depscan::build_dependency_graph(analysis.sources);
    }

    std::vector<CheckTaskPlan> stale;
    stale.reserve(candidates.size());
    for (const auto& task : candidates) {
        const auto found = cache.find(task.relative_path);
        if (found != cache.end() && found->second.mtime == task.mtime &&
            found->second.size == task.size &&
            found->second.dependency_recency == task.recency_rank) {
            continue;
        }
        stale.push_back(task);
    }

    const auto optimization = select_optimization_mode(
        sources.size(), stale.size(), config.build_settings);
    emit_optimization_trace(CompileMode::Check, optimization, sources.size(),
                            stale.size());

    auto provisional_config = with_runtime_capacity(config, stale.size(), profile);
    provisional_config.optimization_mode = optimization.mode;
    provisional_config.optimization_reason = optimization.reason;
    if (provisional_config.optimization_mode == OptimizationMode::Aggressive) {
        provisional_config.pch_file =
            detail::ensure_auto_pch(provisional_config, build_root, sources);
    }

    const auto target_batches = std::max<std::size_t>(
        1, std::min<std::size_t>(
               stale.empty() ? 1 : stale.size(),
               static_cast<std::size_t>(std::max(1, provisional_config.jobs)) * 2));

    const auto batch_plans = make_check_batch_plans(stale, dep_graph, target_batches);

    auto runtime_config = with_runtime_capacity(config, batch_plans.size(), profile);
    runtime_config.optimization_mode = optimization.mode;
    runtime_config.optimization_reason = optimization.reason;
    runtime_config.pch_file = provisional_config.pch_file;
    emit_scheduler_line(runtime_config, batch_plans.size());
    if (!batch_plans.empty()) {
        build::actions::ActionGraph graph;
        std::unordered_map<std::string, build::actions::ActionId> batch_source_to_action;
        for (const auto& batch : batch_plans) {
            std::vector<build::actions::ActionId> deps;
            for (const auto& dep_source : batch.dep_sources) {
                const auto dep = batch_source_to_action.find(dep_source);
                if (dep != batch_source_to_action.end()) {
                    deps.push_back(dep->second);
                }
            }

            const auto action_id = build::actions::append_action(
                graph, build::actions::ActionKind::CheckBatch,
                build::actions::PoolKind::Check, std::move(deps),
                build::actions::CheckBatchPayload{
                    .sources = batch.sources,
                    .profile_keys = batch.profile_keys,
                },
                std::max(1.0, batch.estimated_ms), batch.estimated_peak_mb,
                check_batch_display_name(batch.sources), false, batch.recency_rank);
            for (const auto& source : batch.sources) {
                batch_source_to_action[source_key(source)] = action_id;
            }
        }

        const auto observer = detail::make_progress_observer(
            "Checking", graph, [](const build::actions::ActionNode& node) {
                return node.kind == build::actions::ActionKind::CheckBatch;
            });

        std::vector<TimedSample> timing_samples;
        timing_samples.reserve(stale.size());
        std::mutex timing_mutex;

        GUARD(execute_graph(
            graph, runtime_config.capacity_plan,
            [&runtime_config, &timing_samples, &timing_mutex](
                const build::actions::ActionNode& node,
                const std::atomic_bool* cancel_requested) -> util::Status {
                const auto& payload =
                    std::get<build::actions::CheckBatchPayload>(node.payload);

                auto batch_metrics = detail::run_check_batch(runtime_config,
                                                            payload.sources,
                                                            cancel_requested);
                if (!batch_metrics) {
                    for (std::size_t i = 0; i < payload.sources.size(); ++i) {
                        auto single = detail::check_source(runtime_config,
                                                           payload.sources[i],
                                                           cancel_requested);
                        if (!single) {
                            return std::unexpected(single.error());
                        }
                        if (single->wall_ms > 0.0) {
                            std::lock_guard<std::mutex> lock(timing_mutex);
                            timing_samples.push_back(TimedSample{
                                .key = payload.profile_keys[i],
                                .elapsed_ms = std::max(1.0, single->wall_ms),
                                .peak_mb = single->peak_working_set_mb,
                                .startup_ms = 0.0,
                            });
                        }
                    }
                    return util::Ok;
                }

                if (batch_metrics->wall_ms <= 0.0) {
                    return util::Ok;
                }

                const double per_source_ms =
                    std::max(1.0, batch_metrics->wall_ms /
                                      static_cast<double>(payload.sources.size()));
                const auto peak_sample =
                    payload.sources.size() == 1 ? batch_metrics->peak_working_set_mb
                                                : std::optional<double>{};
                std::lock_guard<std::mutex> lock(timing_mutex);
                for (const auto& key : payload.profile_keys) {
                    timing_samples.push_back(TimedSample{
                        .key = key,
                        .elapsed_ms = per_source_ms,
                        .peak_mb = peak_sample,
                        .startup_ms = 0.0,
                    });
                }
                return util::Ok;
            },
            observer));

        for (const auto& sample : timing_samples) {
            compile_profile::update_task_stats(profile, sample.key, sample.elapsed_ms,
                                               sample.peak_mb, sample.startup_ms);
        }
        GUARD(compile_profile::save_profile(build_root, profile));
    }

    for (const auto& task : stale) {
        cache[task.relative_path] =
            detail::CheckCacheEntry{task.mtime, task.size, task.recency_rank};
    }

    return detail::write_check_cache(cache_file, signature_hash, cache);
}

}  // namespace build::compile

