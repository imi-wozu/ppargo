#include "build/depfile.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>


namespace {

auto collapse_continuations(const std::string& content) -> std::string {
    std::string normalized;
    normalized.reserve(content.size());

    for (std::size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\\' && i + 1 < content.size() &&
            (content[i + 1] == '\n' || content[i + 1] == '\r')) {
            ++i;
            if (content[i] == '\r' && i + 1 < content.size() &&
                content[i + 1] == '\n') {
                ++i;
            }
            while (i + 1 < content.size() &&
                   (content[i + 1] == ' ' || content[i + 1] == '\t')) {
                ++i;
            }
            normalized.push_back(' ');
            continue;
        }
        normalized.push_back(content[i]);
    }

    return normalized;
}

auto find_rule_separator(const std::string& content) -> std::size_t {
    bool escaped = false;
    for (std::size_t i = 0; i < content.size(); ++i) {
        const char c = content[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c != ':') {
            continue;
        }

        const bool drive_prefix =
            i > 0 && std::isalpha(static_cast<unsigned char>(content[i - 1])) &&
            i + 1 < content.size() &&
            (content[i + 1] == '\\' || content[i + 1] == '/');
        if (!drive_prefix) {
            return i;
        }
    }
    return std::string::npos;
}

auto parse_tokens(const std::string& deps_part) -> std::vector<std::string> {
    std::vector<std::string> tokens;
    std::string current;

    for (std::size_t i = 0; i < deps_part.size(); ++i) {
        const char c = deps_part[i];
        if (c == '\\') {
            if (i + 1 < deps_part.size()) {
                const char next = deps_part[i + 1];
                if (next == ' ' || next == '\t' || next == '#' || next == ':' ||
                    next == '\\') {
                    current.push_back(next);
                    ++i;
                    continue;
                }
            }

            // Keep path separators like `C:\repo\src\main.cpp` intact.
            current.push_back('\\');
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        current.push_back(c);
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

}  // namespace

namespace build::depfile {

auto parse_dependencies(const std::filesystem::path& dep_file)
    -> util::Result<std::vector<std::filesystem::path>> {
    std::ifstream input(dep_file, std::ios::binary);
    if (!input.is_open()) {
        return std::unexpected("Build Error: Failed to open depfile: " +
                               dep_file.string());
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    const std::string normalized = collapse_continuations(buffer.str());
    const std::size_t separator = find_rule_separator(normalized);
    if (separator == std::string::npos) {
        return std::unexpected("Build Error: Invalid depfile format: " +
                               dep_file.string());
    }

    const std::string deps_part = normalized.substr(separator + 1);
    const auto tokens = parse_tokens(deps_part);
    if (tokens.empty()) {
        return std::unexpected("Build Error: No dependencies found in depfile: " +
                               dep_file.string());
    }

    std::vector<std::filesystem::path> dependencies;
    dependencies.reserve(tokens.size());
    for (const auto& token : tokens) {
        if (token == "\\") {
            continue;
        }
        dependencies.emplace_back(token);
    }
    if (dependencies.empty()) {
        return std::unexpected("Build Error: No valid dependencies found in depfile: " +
                               dep_file.string());
    }

    return dependencies;
}

}  // namespace build::depfile


