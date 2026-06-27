#include "build/targets.hpp"
#include "build/targets_internal.hpp"
#include "core/paths.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <span>

namespace build::targets {

namespace {

auto is_glob_pattern(std::string_view pattern) -> bool {
    return pattern.find_first_of("*?[") != std::string_view::npos;
}

auto glob_matches(std::string_view pattern, std::string_view text) -> bool {
    std::size_t pattern_index = 0;
    std::size_t text_index = 0;
    std::size_t star_pattern = std::string_view::npos;
    std::size_t star_text = std::string_view::npos;

    auto class_matches = [&](std::size_t& cursor, char ch) -> bool {
        bool matched = false;
        bool negated = false;
        ++cursor;
        if (cursor < pattern.size() && (pattern[cursor] == '!' || pattern[cursor] == '^')) {
            negated = true;
            ++cursor;
        }
        while (cursor < pattern.size() && pattern[cursor] != ']') {
            if (cursor + 2 < pattern.size() && pattern[cursor + 1] == '-' &&
                pattern[cursor + 2] != ']') {
                const char begin = pattern[cursor];
                const char end = pattern[cursor + 2];
                if (begin <= ch && ch <= end) {
                    matched = true;
                }
                cursor += 3;
                continue;
            }
            if (pattern[cursor] == ch) {
                matched = true;
            }
            ++cursor;
        }
        while (cursor < pattern.size() && pattern[cursor] != ']') {
            ++cursor;
        }
        if (cursor < pattern.size() && pattern[cursor] == ']') {
            ++cursor;
        }
        return negated ? !matched : matched;
    };

    while (text_index < text.size()) {
        if (pattern_index < pattern.size()) {
            const char token = pattern[pattern_index];
            if (token == '*') {
                star_pattern = ++pattern_index;
                star_text = text_index;
                continue;
            }
            if (token == '?') {
                ++pattern_index;
                ++text_index;
                continue;
            }
            if (token == '[') {
                auto cursor = pattern_index;
                if (class_matches(cursor, text[text_index])) {
                    pattern_index = cursor;
                    ++text_index;
                    continue;
                }
            } else if (token == text[text_index]) {
                ++pattern_index;
                ++text_index;
                continue;
            }
        }

        if (star_pattern != std::string_view::npos) {
            pattern_index = star_pattern;
            text_index = ++star_text;
            continue;
        }
        return false;
    }

    while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
        ++pattern_index;
    }
    return pattern_index == pattern.size();
}

void append_unique_selection(std::vector<Selection>& selections, Selection selection) {
    const auto duplicate = std::find_if(
        selections.begin(), selections.end(),
        [&](const Selection& existing) {
            return existing.kind == selection.kind && existing.name == selection.name;
        });
    if (duplicate == selections.end()) {
        selections.push_back(std::move(selection));
    }
}

auto match_named_targets(std::span<const Target> targets,
                         const std::vector<std::string>& patterns,
                         SelectionKind kind,
                         std::string_view missing_label)
    -> util::Result<std::vector<Selection>> {
    std::vector<Selection> selections;
    for (const auto& pattern : patterns) {
        std::vector<Selection> matches;
        for (const auto& target : targets) {
            const bool matched =
                is_glob_pattern(pattern) ? glob_matches(pattern, target.name)
                                         : target.name == pattern;
            if (matched) {
                matches.push_back(Selection{.kind = kind, .name = target.name});
            }
        }
        if (matches.empty()) {
            return std::unexpected(util::make_error(std::format(
                "{} target pattern '{}' matched no targets.",
                missing_label, pattern)));
        }
        for (auto& match : matches) {
            append_unique_selection(selections, std::move(match));
        }
    }
    return selections;
}

}  // namespace

auto executable_name(std::string_view base) -> std::string {
    std::string name{base};
#ifdef _WIN32
    if (!name.ends_with(".exe")) {
        name += ".exe";
    }
#endif
    return name;
}

auto resolve_build_target(const std::filesystem::path& root,
                          const core::Manifest& manifest,
                          const std::optional<Selection>& selection)
    -> util::Result<ResolvedBuildTarget> {
    const auto discovery = GUARD(discover(root, manifest));
    const auto source_root{root / manifest.build.source_dir};
    const auto lib_root{source_root / "lib"};

    ResolvedBuildTarget resolved{};
    if (detail::is_directory_path(lib_root)) {
        auto canonical_library_root = GUARD(detail::canonical_existing_path(lib_root));
        resolved.library_roots.push_back(std::move(canonical_library_root));
    } else {
        auto canonical_source_root = GUARD(detail::canonical_existing_path(source_root));
        resolved.library_roots.push_back(std::move(canonical_source_root));
    }

    auto choose_named = [](std::span<const Target>  targets,
                           std::string_view  name) -> std::optional<Target> {
        auto it = std::find_if(
            targets.begin(), targets.end(),
            [&name](const Target& target) { return target.name == name; });
        if (it == targets.end()) {
            return std::nullopt;
        }
        return *it;
    };

    if (!selection.has_value() || selection->kind == SelectionKind::DefaultBin) {
        if (discovery.default_bin.has_value()) {
            resolved.name = discovery.default_bin->name;
            resolved.binary_name = core::binary_name(manifest);
            resolved.target_sources.push_back(discovery.default_bin->main_source);
            return resolved;
        }

        if (discovery.bins.size() == 1) {
            const auto& bin = discovery.bins.front();
            resolved.name = bin.name;
            resolved.binary_name = executable_name(bin.name);
            if (bin.recursive) {
                resolved.target_roots.push_back(bin.source_root);
            } else {
                resolved.target_sources.push_back(bin.main_source);
            }
            return resolved;
        }

        if (discovery.bins.empty()) {
            return std::unexpected(util::make_error("No binary target found. Create src/main.cpp or src/bin/<name>.cpp."));
        }

        return std::unexpected(util::make_error(std::format(
            "Multiple binary targets found. Use --bin <name>. Available: {}",
            detail::format_named_choices(discovery.bins))));
    }

    if (selection->kind == SelectionKind::Bin) {
        if (selection->name.empty()) {
            return std::unexpected(util::make_error("--bin requires a target name."));
        }
        if (discovery.default_bin.has_value() &&
            selection->name == discovery.default_bin->name) {
            resolved.name = discovery.default_bin->name;
            resolved.binary_name = core::binary_name(manifest);
            resolved.target_sources.push_back(discovery.default_bin->main_source);
            return resolved;
        }

        auto selected = choose_named(discovery.bins, selection->name);
        if (!selected.has_value()) {
            return std::unexpected(util::make_error(std::format(
                "Binary target '{}' was not found.", selection->name)));
        }

        resolved.name = selected->name;
        resolved.binary_name = executable_name(selected->name);
        if (selected->recursive) {
            resolved.target_roots.push_back(selected->source_root);
        } else {
            resolved.target_sources.push_back(selected->main_source);
        }
        return resolved;
    }

    if (selection->kind == SelectionKind::Example) {
        if (selection->name.empty()) {
            return std::unexpected(util::make_error("--example requires a target name."));
        }

        auto selected = choose_named(discovery.examples, selection->name);
        if (!selected.has_value()) {
            return std::unexpected(util::make_error(std::format(
                "Example target '{}' was not found.", selection->name)));
        }

        resolved.name = selected->name;
        resolved.binary_name = executable_name(selected->name);
        resolved.target_sources.push_back(selected->main_source);
        return resolved;
    }

    if (selection->kind == SelectionKind::Bench) {
        if (selection->name.empty()) {
            return std::unexpected(util::make_error("--bench requires a target name."));
        }

        auto selected = choose_named(discovery.benches, selection->name);
        if (!selected.has_value()) {
            return std::unexpected(util::make_error(std::format(
                "Bench target '{}' was not found.", selection->name)));
        }

        resolved.name = selected->name;
        resolved.binary_name = executable_name(selected->name);
        resolved.target_sources.push_back(selected->main_source);
        return resolved;
    }

    if (selection->kind == SelectionKind::UnitTest) {
        if (discovery.unit_test_sources.empty()) {
            return std::unexpected(util::make_error("Test Error: No unit tests found under tests/unit."));
        }
        resolved.name = "unit";
        resolved.binary_name = executable_name(manifest.package.name + "_unit_tests");
        resolved.target_sources = discovery.unit_test_sources;
        return resolved;
    }

    if (selection->kind == SelectionKind::IntegrationTest) {
        if (selection->name.empty()) {
            return std::unexpected(util::make_error("Test Error: --test requires an integration test target name."));
        }
        auto selected = choose_named(discovery.integration_tests, selection->name);
        if (!selected.has_value()) {
            return std::unexpected(util::make_error(std::format(
                "Test Error: Integration test '{}' was not found.",
                selection->name)));
        }

        resolved.name = selected->name;
        resolved.binary_name = executable_name("itest_" + selected->name);
        if (selected->recursive) {
            resolved.target_sources = GUARD(detail::collect_recursive_cpp_sources(selected->source_root));
        } else {
            resolved.target_sources.push_back(selected->main_source);
        }
        return resolved;
    }

    return std::unexpected(util::make_error("Unsupported target selection."));
}

auto resolve_build_selections(const std::filesystem::path& root,
                              const core::Manifest& manifest,
                              const BuildSelectionOptions& options)
    -> util::Result<std::vector<Selection>> {
    const auto discovery = GUARD(discover(root, manifest));
    std::vector<Selection> selections;

    const bool explicit_selection =
        options.bins_all || options.examples_all || options.tests_all ||
        options.benches_all || !options.bins.empty() ||
        !options.examples.empty() || !options.tests.empty() ||
        !options.benches.empty();

    auto append_all_bins = [&]() -> util::Status {
        if (discovery.default_bin.has_value()) {
            append_unique_selection(
                selections,
                Selection{.kind = SelectionKind::Bin, .name = discovery.default_bin->name});
        }
        for (const auto& bin : discovery.bins) {
            append_unique_selection(
                selections, Selection{.kind = SelectionKind::Bin, .name = bin.name});
        }
        if (!discovery.default_bin.has_value() && discovery.bins.empty()) {
            return std::unexpected(util::make_error(
                "No binary target found. Create src/main.cpp or src/bin/<name>.cpp."));
        }
        return util::Ok;
    };

    auto append_all_examples = [&]() -> util::Status {
        for (const auto& example : discovery.examples) {
            append_unique_selection(
                selections,
                Selection{.kind = SelectionKind::Example, .name = example.name});
        }
        return util::Ok;
    };

    auto append_all_tests = [&]() -> util::Status {
        if (!discovery.unit_test_sources.empty()) {
            append_unique_selection(
                selections, Selection{.kind = SelectionKind::UnitTest, .name = "unit"});
        }
        for (const auto& test : discovery.integration_tests) {
            append_unique_selection(selections, Selection{
                                                   .kind = SelectionKind::IntegrationTest,
                                                   .name = test.name,
                                               });
        }
        return util::Ok;
    };

    auto append_all_benches = [&]() -> util::Status {
        for (const auto& bench : discovery.benches) {
            append_unique_selection(
                selections,
                Selection{.kind = SelectionKind::Bench, .name = bench.name});
        }
        return util::Ok;
    };

    if (!explicit_selection) {
        GUARD(append_all_bins());
        return selections;
    }

    if (options.bins_all) {
        GUARD(append_all_bins());
    }
    if (options.examples_all) {
        GUARD(append_all_examples());
    }
    if (options.tests_all) {
        GUARD(append_all_tests());
    }
    if (options.benches_all) {
        GUARD(append_all_benches());
    }

    if (!options.bins.empty()) {
        std::vector<Target> bin_targets;
        if (discovery.default_bin.has_value()) {
            bin_targets.push_back(*discovery.default_bin);
        }
        bin_targets.insert(bin_targets.end(), discovery.bins.begin(),
                           discovery.bins.end());
        auto matched = GUARD(match_named_targets(bin_targets, options.bins,
                                                 SelectionKind::Bin, "Binary"));
        for (auto& selection : matched) {
            append_unique_selection(selections, std::move(selection));
        }
    }

    if (!options.examples.empty()) {
        auto matched = GUARD(match_named_targets(discovery.examples, options.examples,
                                                 SelectionKind::Example, "Example"));
        for (auto& selection : matched) {
            append_unique_selection(selections, std::move(selection));
        }
    }

    if (!options.tests.empty()) {
        auto matched = GUARD(match_named_targets(
            discovery.integration_tests, options.tests,
            SelectionKind::IntegrationTest, "Integration test"));
        for (auto& selection : matched) {
            append_unique_selection(selections, std::move(selection));
        }
    }

    if (!options.benches.empty()) {
        auto matched = GUARD(match_named_targets(discovery.benches,
                                                 options.benches,
                                                 SelectionKind::Bench, "Bench"));
        for (auto& selection : matched) {
            append_unique_selection(selections, std::move(selection));
        }
    }

    if (selections.empty()) {
        return std::unexpected(util::make_error(
            "No build targets matched the requested selection."));
    }

    return selections;
}

auto resolve_test_plan(const std::filesystem::path& root,
                       const core::Manifest& manifest,
                       const TestSelectionOptions& options)
    -> util::Result<ResolvedTestPlan> {
    const auto discovery = GUARD(discover(root, manifest));
    ResolvedTestPlan plan{};

    auto append_unique = [](std::vector<Selection>& selections,
                            Selection selection) {
        append_unique_selection(selections, std::move(selection));
    };

    const bool explicit_target_selection =
        options.scope != TestScope::All || options.tests_all ||
        options.examples_all || options.benches_all || !options.tests.empty() ||
        !options.examples.empty() || !options.benches.empty();

    auto add_all_runnable_tests = [&]() -> util::Status {
        if (!discovery.unit_test_sources.empty()) {
            const Selection unit{.kind = SelectionKind::UnitTest, .name = "unit"};
            append_unique(plan.build_targets, unit);
            append_unique(plan.run_targets, unit);
        }
        for (const auto& target : discovery.integration_tests) {
            const Selection integration{
                .kind = SelectionKind::IntegrationTest,
                .name = target.name,
            };
            append_unique(plan.build_targets, integration);
            append_unique(plan.run_targets, integration);
        }
        if (discovery.unit_test_sources.empty() &&
            discovery.integration_tests.empty()) {
            return std::unexpected(util::make_error(
                "Test Error: No tests found. Expected sources under tests/unit or tests/integration."));
        }
        return util::Ok;
    };

    auto add_default_runnable_tests = [&]() -> util::Status {
        if (!discovery.unit_test_sources.empty()) {
            const Selection unit{.kind = SelectionKind::UnitTest, .name = "unit"};
            append_unique(plan.build_targets, unit);
            append_unique(plan.run_targets, unit);
            return util::Ok;
        }

        if (!discovery.integration_tests.empty()) {
            for (const auto& target : discovery.integration_tests) {
                const Selection integration{
                    .kind = SelectionKind::IntegrationTest,
                    .name = target.name,
                };
                append_unique(plan.build_targets, integration);
                append_unique(plan.run_targets, integration);
            }
            return util::Ok;
        }

        return std::unexpected(util::make_error(
            "Test Error: No tests found. Expected sources under tests/unit or tests/integration."));
    };

    auto add_examples = [&](bool runnable) -> util::Status {
        if (discovery.examples.empty()) {
            return std::unexpected(
                util::make_error("No example targets found under examples/."));
        }
        for (const auto& target : discovery.examples) {
            const Selection example{
                .kind = SelectionKind::Example,
                .name = target.name,
            };
            append_unique(plan.build_targets, example);
            if (runnable) {
                append_unique(plan.run_targets, example);
            }
        }
        return util::Ok;
    };

    auto add_benches = [&]() -> util::Status {
        if (discovery.benches.empty()) {
            return std::unexpected(
                util::make_error("No bench targets found under benches/."));
        }
        for (const auto& target : discovery.benches) {
            const Selection bench{
                .kind = SelectionKind::Bench,
                .name = target.name,
            };
            append_unique(plan.build_targets, bench);
            append_unique(plan.run_targets, bench);
        }
        return util::Ok;
    };

    const bool has_non_test_explicit_targets =
        options.examples_all || !options.examples.empty() ||
        options.benches_all || !options.benches.empty();

    if (!explicit_target_selection) {
        GUARD(add_default_runnable_tests());
        if (options.include_default_examples && !discovery.examples.empty()) {
            GUARD(add_examples(false));
        }
        return plan;
    }

    if (options.scope == TestScope::Unit) {
        if (discovery.unit_test_sources.empty()) {
            return std::unexpected(util::make_error(
                "Test Error: No unit tests found under tests/unit."));
        }
        const Selection unit{.kind = SelectionKind::UnitTest, .name = "unit"};
        append_unique(plan.build_targets, unit);
        append_unique(plan.run_targets, unit);
    } else if (options.scope == TestScope::Integration || options.tests_all) {
        if (options.scope == TestScope::Integration && !options.tests_all &&
            options.tests.empty() && discovery.integration_tests.empty() &&
            !has_non_test_explicit_targets) {
            return std::unexpected(util::make_error(
                "Test Error: No integration tests found under tests/integration."));
        }
        if (options.tests_all) {
            GUARD(add_all_runnable_tests());
        } else {
            for (const auto& target : discovery.integration_tests) {
                const Selection integration{
                    .kind = SelectionKind::IntegrationTest,
                    .name = target.name,
                };
                append_unique(plan.build_targets, integration);
                append_unique(plan.run_targets, integration);
            }
        }
    }

    if (!options.tests.empty()) {
        auto matched = GUARD(match_named_targets(
            discovery.integration_tests, options.tests,
            SelectionKind::IntegrationTest, "Integration test"));
        for (auto& selection : matched) {
            append_unique(plan.build_targets, selection);
            append_unique(plan.run_targets, selection);
        }
    }

    if (options.examples_all) {
        GUARD(add_examples(true));
    }
    if (!options.examples.empty()) {
        auto matched = GUARD(match_named_targets(discovery.examples, options.examples,
                                                 SelectionKind::Example, "Example"));
        for (auto& selection : matched) {
            append_unique(plan.build_targets, selection);
            append_unique(plan.run_targets, selection);
        }
    }

    if (options.benches_all) {
        GUARD(add_benches());
    }
    if (!options.benches.empty()) {
        auto matched = GUARD(match_named_targets(discovery.benches, options.benches,
                                                 SelectionKind::Bench, "Bench"));
        for (auto& selection : matched) {
            append_unique(plan.build_targets, selection);
            append_unique(plan.run_targets, selection);
        }
    }

    if (plan.build_targets.empty()) {
        return std::unexpected(util::make_error(
            "No test targets matched the requested selection."));
    }

    return plan;
}

}  // namespace build::targets


