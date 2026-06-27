#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "build/compile.hpp"
#include "build/source_scan.hpp"
#include "util/result.hpp"

namespace build::depscan {

struct AnalyzedSource {
    std::filesystem::path source;
    source_scan::SourceKind kind{source_scan::SourceKind::TranslationUnit};
    std::string provides;
    std::vector<std::string> imports;
    std::vector<std::filesystem::path> file_deps;
    long long recency_rank = 0;
};

struct AnalysisResult {
    std::vector<AnalyzedSource> sources;
    bool used_clang_scan_deps = false;
    bool from_cache = false;
};

struct DependencyGraph {
    std::unordered_map<std::string, std::vector<std::string>> deps;
    std::unordered_map<std::string, std::size_t> depths;
    bool had_cycle = false;
};

auto analyze_sources(const std::filesystem::path& root,
                     const std::filesystem::path& build_root,
                     std::span<const std::filesystem::path> sources,
                     const compile::CompilerConfig& config,
                     std::span<const std::filesystem::path> depfiles = {})
    -> util::Result<AnalysisResult>;

auto build_dependency_graph(std::span<const AnalyzedSource> sources)
    -> DependencyGraph;

}  // namespace build::depscan
