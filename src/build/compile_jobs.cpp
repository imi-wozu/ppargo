#include "build/compile_jobs.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "build/settings.hpp"
#include "core/paths.hpp"
#include "util/environment.hpp"
#include "util/output.hpp"

namespace build::compile::detail {

auto env_var_present(const char* name) -> bool {
    return name != nullptr && name[0] != '\0' && util::env::exists(name);
}

auto parse_forced_jobs_override() -> std::optional<int> {
    const auto override_value = util::env::get("PPARGO_FORCE_COMPILE_JOBS");
    if (!override_value.has_value()) {
        return std::nullopt;
    }

    int parsed = 0;
    const auto* begin = override_value->data();
    const auto* end = begin + override_value->size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return std::clamp(parsed, 1, 64);
}

auto query_available_memory_mb() -> std::optional<std::uint64_t> {
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
        return std::nullopt;
    }
    return status.ullAvailPhys / (1024ULL * 1024ULL);
#else
    return std::nullopt;
#endif
}

auto mode_name(CompileMode mode) -> std::string_view {
    return mode == CompileMode::Check ? "check" : "build";
}

auto scheduler_trace_enabled() -> bool {
    return env_var_present("PPARGO_TRACE_INCREMENTAL") ||
           env_var_present("PPARGO_TRACE");
}

auto incremental_trace_enabled() -> bool {
    return env_var_present("PPARGO_TRACE_INCREMENTAL");
}

auto compile_flags(const core::Manifest& manifest, bool release,
                   bool exceptions_enabled) -> std::vector<std::string> {
    std::vector<std::string> flags;
    if (manifest.package.edition == "cpp26") {
        flags.push_back("-std=c++26");
    } else if (manifest.package.edition == "cpp23") {
        flags.push_back("-std=c++23");
    } else if (manifest.package.edition == "cpp20") {
        flags.push_back("-std=c++20");
    } else {
        flags.push_back("-std=c++17");
    }

    if (release) {
        flags.push_back("-O3");
    } else {
        flags.push_back("-O0");
        flags.push_back("-g");
    }

#ifdef _WIN32
    flags.push_back("-fms-runtime-lib=dll");
#endif
    flags.push_back("-Wall");
    flags.push_back("-Wextra");

    if (!exceptions_enabled) {
        flags.push_back("-fno-exceptions");
    }
    return flags;
}

auto include_paths_for_manifest(const std::filesystem::path& root,
                                const core::Manifest& manifest,
                                std::span<const std::filesystem::path> dependency_include_paths)
    -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> include_paths;
    include_paths.reserve(manifest.build.include_dirs.size() +
                          dependency_include_paths.size());
    for (const auto& include_dir : manifest.build.include_dirs) {
        include_paths.push_back(root / include_dir);
    }
    include_paths.insert(include_paths.end(), dependency_include_paths.begin(),
                         dependency_include_paths.end());
    return include_paths;
}

auto compiler_for_manifest(const core::Manifest& manifest)
    -> std::filesystem::path {
    return manifest.toolchain.compiler.empty()
               ? std::filesystem::path("clang++")
               : std::filesystem::path(manifest.toolchain.compiler);
}

}  // namespace build::compile::detail

namespace build::compile {

namespace {

constexpr std::uint64_t kReserveMb = 1536;
constexpr std::uint32_t kDefaultCompilePeakMb = 192;
constexpr std::uint32_t kDefaultCheckPeakMb = 256;

auto average_peak_mb_for_mode(const compile_profile::CompileProfile& profile,
                              std::string_view mode_tag, std::uint32_t fallback)
    -> std::uint32_t {
    double total = 0.0;
    std::size_t count = 0;
    for (const auto& [key, stats] : profile.stats) {
        if (!key.starts_with(mode_tag) || stats.ewma_peak_mb <= 0.0) {
            continue;
        }
        total += stats.ewma_peak_mb;
        ++count;
    }
    if (count == 0) {
        return fallback;
    }
    return static_cast<std::uint32_t>(
        std::max<double>(1.0, total / static_cast<double>(count)));
}

}  // namespace

auto make_capacity_plan(CompileMode mode, std::size_t ready_task_count,
                        std::optional<int> forced_jobs,
                        const compile_profile::CompileProfile& profile)
    -> scheduler_runtime::CapacityPlan {
    scheduler_runtime::CapacityPlan plan{};
    plan.reserve_mb = kReserveMb;

    const std::uint32_t default_action_mb =
        mode == CompileMode::Check
            ? average_peak_mb_for_mode(profile, "check|", kDefaultCheckPeakMb)
            : average_peak_mb_for_mode(profile, "build|",
                                       kDefaultCompilePeakMb);

    if (ready_task_count == 0) {
        plan.total_slots = 1;
        plan.memory_budget_mb = kReserveMb + default_action_mb;
        plan.reason = "no tasks";
        plan.pools = {
            {.kind = actions::PoolKind::Compile, .depth = 1},
            {.kind = actions::PoolKind::Check, .depth = 1},
            {.kind = actions::PoolKind::Link, .depth = 1},
            {.kind = actions::PoolKind::Console, .depth = 1},
        };
        return plan;
    }

    if (forced_jobs.has_value()) {
        const int clamped = std::clamp(*forced_jobs, 1, 64);
        plan.total_slots = clamped;
        plan.memory_budget_mb =
            kReserveMb +
            static_cast<std::uint64_t>(clamped) * default_action_mb;
        plan.reason = std::format("forced override={}", clamped);
        plan.pools = {
            {.kind = actions::PoolKind::Compile, .depth = clamped},
            {.kind = actions::PoolKind::Check, .depth = clamped},
            {.kind = actions::PoolKind::Link, .depth = 1},
            {.kind = actions::PoolKind::Console, .depth = 1},
        };
        return plan;
    }

    const unsigned hw_threads = std::thread::hardware_concurrency();
    const auto hw_cap = std::clamp<std::size_t>(
        hw_threads == 0 ? 1 : static_cast<std::size_t>(hw_threads), 1, 64);
    const auto base_slots = std::min<std::size_t>(
        {hw_cap, ready_task_count, static_cast<std::size_t>(64)});

    if (ready_task_count <= 8) {
        const auto fast_slots =
            std::clamp<std::size_t>(std::min(hw_cap, ready_task_count), 1, 64);
        plan.total_slots = static_cast<int>(fast_slots);
        plan.memory_budget_mb =
            kReserveMb +
            static_cast<std::uint64_t>(fast_slots) * default_action_mb;
        plan.reason = std::format("small workload: stale={} slots={}",
                                  ready_task_count, fast_slots);
        plan.pools = {
            {.kind = actions::PoolKind::Compile, .depth = plan.total_slots},
            {.kind = actions::PoolKind::Check, .depth = plan.total_slots},
            {.kind = actions::PoolKind::Link, .depth = 1},
            {.kind = actions::PoolKind::Console, .depth = 1},
        };
        return plan;
    }

    std::uint64_t memory_budget =
        kReserveMb + static_cast<std::uint64_t>(base_slots) * default_action_mb;
    if (const auto available_mb = detail::query_available_memory_mb();
        available_mb.has_value()) {
        memory_budget = std::max<std::uint64_t>(*available_mb,
                                                kReserveMb + default_action_mb);
    }

    const auto usable_mb =
        memory_budget > kReserveMb ? memory_budget - kReserveMb : 0;
    const auto memory_slots = std::max<std::size_t>(
        1, static_cast<std::size_t>(usable_mb / default_action_mb));
    const auto total_slots =
        std::clamp<std::size_t>(std::min(base_slots, memory_slots), 1, 64);

    plan.total_slots = static_cast<int>(total_slots);
    plan.memory_budget_mb = memory_budget;
    plan.reason = std::format("base={} mem_budget={}MB action_estimate={}MB",
                              base_slots, memory_budget, default_action_mb);
    plan.pools = {
        {.kind = actions::PoolKind::Compile, .depth = plan.total_slots},
        {.kind = actions::PoolKind::Check, .depth = plan.total_slots},
        {.kind = actions::PoolKind::Link, .depth = 1},
        {.kind = actions::PoolKind::Console, .depth = 1},
    };
    return plan;
}

auto make_compiler_config(const std::filesystem::path& root,
                          const core::Manifest& manifest, bool release,
                          bool exceptions_enabled, CompileMode mode,
                          std::size_t task_count,
                          const std::optional<std::filesystem::path>&
                              output_dir_override,
                          std::span<const std::filesystem::path> dependency_include_paths)
    -> util::Result<CompilerConfig> {
    CompilerConfig config;
    config.compiler = detail::compiler_for_manifest(manifest);
    config.flags = detail::compile_flags(manifest, release, exceptions_enabled);
    config.include_paths =
        detail::include_paths_for_manifest(root, manifest, dependency_include_paths);
    config.modules_enabled = manifest.build.modules;
    config.module_interface_exts = manifest.build.module_interface_exts;
    config.module_output_dir =
        root / core::effective_module_output_dir(manifest, output_dir_override);
    config.build_settings = build::settings::resolve(manifest);
    config.mode = mode;

    const compile_profile::CompileProfile empty_profile;
    config.capacity_plan = make_capacity_plan(
        mode, task_count, detail::parse_forced_jobs_override(), empty_profile);
    config.jobs = config.capacity_plan.total_slots;
    config.jobs_reason = config.capacity_plan.reason;

    return config;
}

}  // namespace build::compile
