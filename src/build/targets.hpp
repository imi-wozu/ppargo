#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/manifest.hpp"
#include "util/result.hpp"

namespace build::targets {

enum class TestScope {
    All,
    Unit,
    Integration,
};

enum class SelectionKind {
    DefaultBin,
    Bin,
    Example,
    Bench,
    UnitTest,
    IntegrationTest,
};

struct Selection {
    SelectionKind kind{SelectionKind::DefaultBin};
    std::string name;
};

struct BuildSelectionOptions {
    std::vector<std::string> bins;
    bool bins_all{false};
    std::vector<std::string> examples;
    bool examples_all{false};
    std::vector<std::string> tests;
    bool tests_all{false};
    std::vector<std::string> benches;
    bool benches_all{false};
};

struct TestSelectionOptions {
    TestScope scope{TestScope::All};
    std::vector<std::string> tests;
    bool tests_all{false};
    std::vector<std::string> examples;
    bool examples_all{false};
    std::vector<std::string> benches;
    bool benches_all{false};
    bool include_default_examples{false};
};

struct ResolvedTestPlan {
    std::vector<Selection> build_targets;
    std::vector<Selection> run_targets;
};

enum class TargetKind {
    Bin,
    Example,
    Bench,
    IntegrationTest,
};

struct Target {
    TargetKind kind{TargetKind::Bin};
    std::string name;
    std::filesystem::path main_source;
    std::filesystem::path source_root;
    bool recursive{false};
};

struct Discovery {
    std::optional<Target> default_bin;
    std::vector<Target> bins;
    std::vector<Target> examples;
    std::vector<Target> benches;
    std::vector<Target> integration_tests;
    std::vector<std::filesystem::path> unit_test_sources;
};

struct ResolvedBuildTarget {
    std::string name;
    std::string binary_name;
    std::vector<std::filesystem::path> library_roots;
    std::vector<std::filesystem::path> target_roots;
    std::vector<std::filesystem::path> target_sources;
};

auto executable_name(std::string_view base) -> std::string;

auto discover(const std::filesystem::path& root, const core::Manifest& manifest)
    -> util::Result<Discovery>;

auto resolve_build_target(const std::filesystem::path& root,
                          const core::Manifest& manifest,
                          const std::optional<Selection>& selection)
    -> util::Result<ResolvedBuildTarget>;

auto resolve_build_selections(const std::filesystem::path& root,
                              const core::Manifest& manifest,
                              const BuildSelectionOptions& options)
    -> util::Result<std::vector<Selection>>;

auto resolve_test_plan(const std::filesystem::path& root,
                       const core::Manifest& manifest,
                       const TestSelectionOptions& options)
    -> util::Result<ResolvedTestPlan>;

}  // namespace build::targets
