#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace build::actions {

enum class ActionKind {
    Compile,
    CheckBatch,
    Link,
    CopyRuntime,
};

enum class PoolKind {
    Compile,
    Check,
    Link,
    Console,
};

struct ActionId {
    std::size_t value = 0;

    auto operator==(const ActionId& other) const -> bool = default;
};

struct CompileActionPayload {
    std::filesystem::path source;
    std::filesystem::path object_file;
    std::filesystem::path dep_file;
    std::string profile_key;
};

struct CheckBatchPayload {
    std::vector<std::filesystem::path> sources;
    std::vector<std::string> profile_keys;
};

struct LinkActionPayload {
    std::vector<std::filesystem::path> objects;
    std::filesystem::path output;
    std::vector<std::filesystem::path> library_paths;
    std::vector<std::string> libraries;
    std::filesystem::path link_signature_file;
    std::string link_signature;
    bool release = false;
};

struct CopyRuntimePayload {
    std::filesystem::path build_root;
    std::vector<std::filesystem::path> runtime_files;
};

using ActionPayload =
    std::variant<CompileActionPayload, CheckBatchPayload, LinkActionPayload,
                 CopyRuntimePayload>;

struct ActionNode {
    ActionId id;
    ActionKind kind = ActionKind::Compile;
    PoolKind pool = PoolKind::Compile;
    std::vector<ActionId> deps;
    ActionPayload payload;
    double estimated_ms = 1.0;
    std::uint32_t estimated_peak_mb = 1;
    std::int64_t recency_rank = 0;
    std::string display_name;
    bool requires_console = false;
};

struct ActionGraph {
    std::vector<ActionNode> nodes;
};

auto append_action(ActionGraph& graph, ActionKind kind, PoolKind pool,
                   std::vector<ActionId> deps, ActionPayload payload,
                   double estimated_ms, std::uint32_t estimated_peak_mb,
                   std::string display_name, bool requires_console = false,
                   std::int64_t recency_rank = 0) -> ActionId;

auto pool_name(PoolKind pool) -> std::string_view;

}  // namespace build::actions
