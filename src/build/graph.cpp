#include "build/graph.hpp"

#include <span>
#include <string>

namespace build::graph {

auto glob_to_regex(std::string_view  pattern) -> std::string {
    std::string regex = "^";
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char ch = pattern[i];
        if (ch == '*') {
            if (i + 1 < pattern.size() && pattern[i + 1] == '*') {
                regex += ".*";
                ++i;
            } else {
                regex += "[^/]*";
            }
            continue;
        }
        if (ch == '?') {
            regex += ".";
            continue;
        }

        const std::string special = R"(\.^$|()[]{}+)";
        if (special.find(ch) != std::string::npos) {
            regex += '\\';
        }
        regex += (ch == '\\') ? '/' : ch;
    }
    regex += "$";
    return regex;
}

auto compile_excludes(std::span<const std::string>  patterns) -> CompiledExcludes {
    CompiledExcludes compiled;
    compiled.reserve(patterns.size());
    for (const auto& pattern : patterns) {
        compiled.emplace_back(glob_to_regex(pattern));
    }
    return compiled;
}

auto matches_excludes(std::string_view  relative_path,
                      const CompiledExcludes& compiled_patterns) -> bool {
    for (const auto& pattern : compiled_patterns) {
        if (std::regex_match(std::string(relative_path), pattern)) {
            return true;
        }
    }
    return false;
}

auto matches_excludes(std::string_view  relative_path,
                      std::span<const std::string>  patterns) -> bool {
    const auto compiled = compile_excludes(patterns);
    return matches_excludes(relative_path, compiled);
}

auto object_path_for_source(
    const std::filesystem::path& source_root, const std::filesystem::path& obj_root,
    const std::filesystem::path& source_file)
    -> util::Result<std::filesystem::path> {
    if (source_root.empty() || obj_root.empty()) {
        return std::unexpected(util::make_error("Source root and object root must be set."));
    }

    std::error_code ec;
    std::filesystem::path rel =
        std::filesystem::relative(source_file, source_root, ec);
    if (ec) {
        return std::unexpected(util::make_error("Failed to map source path to object path: " +
                               source_file.string()));
    }
    std::filesystem::path obj = obj_root / rel;
    obj.replace_extension(".o");
    return obj;
}

}  // namespace build::graph

