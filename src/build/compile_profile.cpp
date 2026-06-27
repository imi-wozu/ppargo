#include "build/compile_profile.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "util/fs.hpp"

namespace {

constexpr std::string_view kProfileHeaderV1 = "ppargo-compile-profile-v1";
constexpr std::string_view kProfileHeaderV2 = "ppargo-compile-profile-v2";

auto parse_double(std::string_view text) -> std::optional<double> {
    std::string owned{text};
    char* end = nullptr;
    const double value = std::strtod(owned.c_str(), &end);
    if (end == owned.c_str() || end == nullptr || *end != '\0') {
        return std::nullopt;
    }
    return value;
}

auto parse_uint(std::string_view text) -> std::optional<std::uint32_t> {
    std::string owned{text};
    char* end = nullptr;
    const unsigned long value = std::strtoul(owned.c_str(), &end, 10);
    if (end == owned.c_str() || end == nullptr || *end != '\0') {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

auto split_tabs(std::string_view  line) -> std::vector<std::string_view> {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= line.size()) {
        const auto end = line.find('\t', start);
        const auto part_end = end == std::string::npos ? line.size() : end;
        parts.emplace_back(line.data() + start, part_end - start);
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

auto blend(double current, double sample) -> double {
    return current * 0.7 + sample * 0.3;
}

}  // namespace

namespace build::compile_profile {

auto profile_path_for_build_root(const std::filesystem::path& build_root)
    -> std::filesystem::path {
    return build_root.parent_path() / ".compile_profile.json";
}

auto make_task_key(const std::filesystem::path& source,
                   std::string_view signature_hash,
                   std::string_view mode_tag) -> std::string {
    return std::format("{}|{}|{}", mode_tag, signature_hash,
                       source.generic_string());
}

auto load_profile(const std::filesystem::path& build_root)
    -> util::Result<CompileProfile> {
    CompileProfile profile;
    const auto path = profile_path_for_build_root(build_root);

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return profile;
    }
    if (ec) {
        return std::unexpected(util::make_error(std::format("Failed to access compile profile: {} ({})",
                        path.string(), ec.message())));
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::unexpected(util::make_error("Failed to open compile profile: " + path.string()));
    }

    std::string header;
    if (!std::getline(input, header)) {
        return profile;
    }

    bool v1 = false;
    if (header == kProfileHeaderV1) {
        v1 = true;
    } else if (header != kProfileHeaderV2) {
        return std::unexpected(util::make_error("Unsupported compile profile format."));
    }

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        const auto parts = split_tabs(line);
        if (v1) {
            if (parts.size() != 2) {
                continue;
            }
            const auto parsed_ms = parse_double(parts[1]);
            if (!parsed_ms.has_value()) {
                continue;
            }
            profile.stats[std::string(parts[0])] = TaskStats{
                .ewma_ms = std::max(1.0, *parsed_ms),
                .ewma_peak_mb = 0.0,
                .ewma_startup_ms = 0.0,
                .samples = 1,
            };
            continue;
        }

        if (parts.size() != 5) {
            continue;
        }
        const auto parsed_ms = parse_double(parts[1]);
        const auto parsed_peak = parse_double(parts[2]);
        const auto parsed_startup = parse_double(parts[3]);
        const auto parsed_samples = parse_uint(parts[4]);
        if (!parsed_ms.has_value() || !parsed_peak.has_value() ||
            !parsed_startup.has_value() || !parsed_samples.has_value()) {
            continue;
        }
        profile.stats[std::string(parts[0])] = TaskStats{
            .ewma_ms = std::max(1.0, *parsed_ms),
            .ewma_peak_mb = std::max(0.0, *parsed_peak),
            .ewma_startup_ms = std::max(0.0, *parsed_startup),
            .samples = *parsed_samples,
        };
    }

    return profile;
}

auto save_profile(const std::filesystem::path& build_root,
                  const CompileProfile& profile) -> util::Status {
    const auto path = profile_path_for_build_root(build_root);

    std::vector<std::pair<std::string, TaskStats>> entries;
    entries.reserve(profile.stats.size());
    for (const auto& [key, value] : profile.stats) {
        entries.emplace_back(key, value);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });

    std::ostringstream out;
    out << kProfileHeaderV2 << "\n";
    for (const auto& [key, value] : entries) {
        out << key << "\t" << value.ewma_ms << "\t" << value.ewma_peak_mb << "\t"
            << value.ewma_startup_ms << "\t" << value.samples << "\n";
    }
    return util::fs::atomic_write_text_result(path, out.str());
}

auto lookup_stats(const CompileProfile& profile, std::string_view key)
    -> std::optional<TaskStats> {
    const auto found = profile.stats.find(std::string(key));
    if (found == profile.stats.end()) {
        return std::nullopt;
    }
    return found->second;
}

auto lookup_cost_ms(const CompileProfile& profile, std::string_view key)
    -> std::optional<double> {
    const auto found = profile.stats.find(std::string(key));
    if (found == profile.stats.end()) {
        return std::nullopt;
    }
    return found->second.ewma_ms;
}

auto lookup_peak_mb(const CompileProfile& profile, std::string_view key)
    -> std::optional<double> {
    const auto found = profile.stats.find(std::string(key));
    if (found == profile.stats.end() || found->second.ewma_peak_mb <= 0.0) {
        return std::nullopt;
    }
    return found->second.ewma_peak_mb;
}

auto update_task_stats(CompileProfile& profile, std::string_view key,
                       double sample_ms, std::optional<double> peak_mb,
                       double startup_ms) -> void {
    const double bounded_ms = std::max(1.0, sample_ms);
    const double bounded_startup = std::max(0.0, startup_ms);

    auto& stats = profile.stats[std::string(key)];
    if (stats.samples == 0) {
        stats.ewma_ms = bounded_ms;
        stats.ewma_peak_mb = peak_mb.has_value() ? std::max(0.0, *peak_mb) : 0.0;
        stats.ewma_startup_ms = bounded_startup;
        stats.samples = 1;
        return;
    }

    stats.ewma_ms = blend(stats.ewma_ms, bounded_ms);
    if (peak_mb.has_value()) {
        stats.ewma_peak_mb = blend(stats.ewma_peak_mb, std::max(0.0, *peak_mb));
    }
    stats.ewma_startup_ms = blend(stats.ewma_startup_ms, bounded_startup);
    ++stats.samples;
}

}  // namespace build::compile_profile


