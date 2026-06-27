#include "build/targets.hpp"
#include "build/targets_internal.hpp"

#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <string>
#include <system_error>

namespace build::targets {

auto discover(const std::filesystem::path& root, const core::Manifest& manifest)
    -> util::Result<Discovery> {
    Discovery discovery{};

    const std::filesystem::path source_root{root / manifest.build.source_dir};
    const std::filesystem::path default_main{source_root / "main.cpp"};
    if (detail::is_regular_file_path(default_main)) {
        auto default_main_canonical = GUARD(detail::canonical_existing_path(default_main));
        auto source_root_canonical = GUARD(detail::canonical_existing_path(source_root));
        discovery.default_bin = Target{
            .kind = TargetKind::Bin,
            .name = manifest.package.name,
            .main_source = std::move(default_main_canonical),
            .source_root = std::move(source_root_canonical),
            .recursive = false,
        };
    }

    std::map<std::string, Target> bin_index{};
    const std::filesystem::path bin_root{source_root / "bin"};
    std::error_code ec;
    if (std::filesystem::exists(bin_root, ec) && !ec) {
        for (std::filesystem::directory_iterator it(bin_root, ec), end; it != end;
             it.increment(ec)) {
            if (ec) {
                return std::unexpected(util::make_error(std::format(
                    "Failed while scanning bin targets: {} ({})",
                    bin_root.string(), ec.message())));
            }

            if (it->is_regular_file(ec)) {
                if (ec) {
                    return std::unexpected(util::make_error(std::format(
                        "Failed to inspect bin target entry: {} ({})",
                        it->path().string(), ec.message())));
                }
                if (!detail::is_cpp_source_extension(it->path().extension().string())) {
                    continue;
                }
                const auto name{it->path().stem().string()};
                if (name.empty()) {
                    continue;
                }
                if (bin_index.contains(name)) {
                    return std::unexpected(util::make_error(std::format(
                        "Duplicate binary target '{}' in src/bin.",
                        name)));
                }
                auto main_source_canonical = GUARD(detail::canonical_existing_path(it->path()));
                auto bin_root_canonical = GUARD(detail::canonical_existing_path(bin_root));
                bin_index.emplace(
                    name, Target{
                              .kind = TargetKind::Bin,
                              .name = name,
                              .main_source = std::move(main_source_canonical),
                              .source_root = std::move(bin_root_canonical),
                              .recursive = false,
                          });
                continue;
            }

            if (it->is_directory(ec)) {
                if (ec) {
                    return std::unexpected(util::make_error(std::format(
                        "Failed to inspect bin target entry: {} ({})",
                        it->path().string(), ec.message())));
                }
                const auto main_source{it->path() / "main.cpp"};
                if (!detail::is_regular_file_path(main_source)) {
                    continue;
                }
                const auto name{it->path().filename().string()};
                if (name.empty()) {
                    continue;
                }
                if (bin_index.contains(name)) {
                    return std::unexpected(util::make_error(std::format(
                        "Duplicate binary target '{}' in src/bin.",
                        name)));
                }
                auto main_source_canonical = GUARD(detail::canonical_existing_path(main_source));
                auto source_root_canonical = GUARD(detail::canonical_existing_path(it->path()));
                bin_index.emplace(
                    name, Target{
                              .kind = TargetKind::Bin,
                              .name = name,
                              .main_source = std::move(main_source_canonical),
                              .source_root = std::move(source_root_canonical),
                              .recursive = true,
                          });
            }
        }
    }
    if (ec) {
        return std::unexpected(util::make_error(std::format(
            "Failed to access bin target directory: {} ({})",
            bin_root.string(), ec.message())));
    }
    for (auto& [_, target] : bin_index) {
        discovery.bins.push_back(std::move(target));
    }
    detail::sort_targets(discovery.bins);

    const std::filesystem::path examples_root{root / "examples"};
    if (std::filesystem::exists(examples_root, ec) && !ec) {
        std::set<std::string> names{};
        for (std::filesystem::directory_iterator it(examples_root, ec), end; it != end;
             it.increment(ec)) {
            if (ec) {
                return std::unexpected(util::make_error(std::format(
                    "Failed while scanning examples: {} ({})",
                    examples_root.string(), ec.message())));
            }
            if (!it->is_regular_file(ec)) {
                if (ec) {
                    return std::unexpected(util::make_error(std::format(
                        "Failed to inspect example entry: {} ({})",
                        it->path().string(), ec.message())));
                }
                continue;
            }
            if (!detail::is_cpp_source_extension(it->path().extension().string())) {
                continue;
            }
            const auto name{it->path().stem().string()};
            if (!names.insert(name).second) {
                return std::unexpected(util::make_error(std::format(
                    "Duplicate example target '{}'.", name)));
            }
            auto example_main_canonical = GUARD(detail::canonical_existing_path(it->path()));
            auto examples_root_canonical = GUARD(detail::canonical_existing_path(examples_root));
            discovery.examples.push_back(Target{
                .kind = TargetKind::Example,
                .name = name,
                .main_source = std::move(example_main_canonical),
                .source_root = std::move(examples_root_canonical),
                .recursive = false,
            });
        }
    }
    if (ec) {
        return std::unexpected(util::make_error(std::format(
            "Failed to access examples directory: {} ({})",
            examples_root.string(), ec.message())));
    }
    detail::sort_targets(discovery.examples);

    const std::filesystem::path benches_root{root / "benches"};
    if (std::filesystem::exists(benches_root, ec) && !ec) {
        for (std::filesystem::directory_iterator it(benches_root, ec), end; it != end;
             it.increment(ec)) {
            if (ec) {
                return std::unexpected(util::make_error(std::format(
                    "Failed while scanning benches: {} ({})",
                    benches_root.string(), ec.message())));
            }
            if (!it->is_regular_file(ec)) {
                if (ec) {
                    return std::unexpected(util::make_error(std::format(
                        "Failed to inspect bench entry: {} ({})",
                        it->path().string(), ec.message())));
                }
                continue;
            }
            if (!detail::is_cpp_source_extension(it->path().extension().string())) {
                continue;
            }
            auto bench_main_canonical = GUARD(detail::canonical_existing_path(it->path()));
            auto benches_root_canonical = GUARD(detail::canonical_existing_path(benches_root));
            discovery.benches.push_back(Target{
                .kind = TargetKind::Bench,
                .name = it->path().stem().string(),
                .main_source = std::move(bench_main_canonical),
                .source_root = std::move(benches_root_canonical),
                .recursive = false,
            });
        }
    }
    if (ec) {
        return std::unexpected(util::make_error(std::format(
            "Failed to access benches directory: {} ({})",
            benches_root.string(), ec.message())));
    }
    detail::sort_targets(discovery.benches);

    const std::filesystem::path tests_root{root / "tests"};
    const std::filesystem::path unit_root{tests_root / "unit"};
    discovery.unit_test_sources = GUARD(detail::collect_recursive_cpp_sources(unit_root));
    if (discovery.unit_test_sources.empty()) {
        const std::filesystem::path legacy_unit_root{tests_root / "cpp" / "unit"};
        discovery.unit_test_sources = GUARD(detail::collect_recursive_cpp_sources(legacy_unit_root));
    }

    const std::filesystem::path integration_root{tests_root / "integration"};
    if (std::filesystem::exists(integration_root, ec) && !ec) {
        std::map<std::string, Target> integration_index{};

        for (std::filesystem::directory_iterator it(integration_root, ec), end; it != end;
             it.increment(ec)) {
            if (ec) {
                return std::unexpected(util::make_error(std::format(
                    "Failed while scanning integration tests: {} ({})",
                    integration_root.string(), ec.message())));
            }

            const auto filename{it->path().filename().string()};
            if (filename == "fixtures") {
                continue;
            }

            if (it->is_regular_file(ec)) {
                if (ec) {
                    return std::unexpected(util::make_error(std::format(
                        "Failed to inspect integration test entry: {} ({})",
                        it->path().string(), ec.message())));
                }
                if (!detail::is_cpp_source_extension(it->path().extension().string())) {
                    continue;
                }
                const auto name{it->path().stem().string()};
                auto main_source_canonical = GUARD(detail::canonical_existing_path(it->path()));
                auto integration_root_canonical = GUARD(detail::canonical_existing_path(integration_root));
                integration_index.emplace(
                    name, Target{
                              .kind = TargetKind::IntegrationTest,
                              .name = name,
                              .main_source = std::move(main_source_canonical),
                              .source_root = std::move(integration_root_canonical),
                              .recursive = false,
                          });
                continue;
            }

            if (it->is_directory(ec)) {
                if (ec) {
                    return std::unexpected(util::make_error(std::format(
                        "Failed to inspect integration test entry: {} ({})",
                        it->path().string(), ec.message())));
                }
                const auto main_source{it->path() / "main.cpp"};
                if (!detail::is_regular_file_path(main_source)) {
                    continue;
                }
                auto main_source_canonical = GUARD(detail::canonical_existing_path(main_source));
                auto source_root_canonical = GUARD(detail::canonical_existing_path(it->path()));
                integration_index.emplace(
                    filename, Target{
                                  .kind = TargetKind::IntegrationTest,
                                  .name = filename,
                                  .main_source = std::move(main_source_canonical),
                                  .source_root = std::move(source_root_canonical),
                                  .recursive = true,
                              });
            }
        }

        for (auto& [_, target] : integration_index) {
            discovery.integration_tests.push_back(std::move(target));
        }
    }
    if (ec) {
        return std::unexpected(util::make_error(std::format(
            "Failed to access integration test directory: {} ({})",
            integration_root.string(), ec.message())));
    }
    detail::sort_targets(discovery.integration_tests);

    return discovery;
}

}  // namespace build::targets


