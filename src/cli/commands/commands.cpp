#include "cli/commands/commands.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "build/compile_jobs.hpp"
#include "cli/help.hpp"
#include "core/manifest.hpp"
#include "core/paths.hpp"
#include "core/system.hpp"
#include "util/output.hpp"
#include "util/process.hpp"
#include "util/result.hpp"

namespace cli {

namespace {

auto trim_trailing_whitespace(std::string value) -> std::string {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' ||
                              value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    return value;
}

auto first_output_line(std::string_view output) -> std::string {
    const auto newline = output.find_first_of("\r\n");
    if (newline == std::string_view::npos) {
        return trim_trailing_whitespace(std::string(output));
    }
    return trim_trailing_whitespace(std::string(output.substr(0, newline)));
}

auto capture_output_options(const std::optional<std::filesystem::path>&
                                working_directory = std::nullopt)
    -> util::process::RunOptions {
    util::process::RunOptions options{};
    options.capture_output = true;
    options.merge_stderr = true;
    options.stdin_mode = util::process::StdinMode::Null;
    options.working_directory = working_directory;
    return options;
}

auto describe_tool_version(const std::filesystem::path& program,
                           std::string_view configured_name) -> std::string {
    const std::array<std::string, 1> args{"--version"};
    const auto result =
        util::process::run(program, args, capture_output_options());
    if (!result.has_value() || result->exit_code != 0) {
        return std::format("{} (version unavailable)", configured_name);
    }

    auto line = first_output_line(result->output);
    if (line.empty()) {
        return std::format("{} (version unavailable)", configured_name);
    }
    return line;
}

struct GitMetadata {
    std::string short_hash;
    std::string full_hash;
    std::string commit_date;
};

struct ToolIdentity {
    std::filesystem::path repo_root;
    std::string name = "ppargo";
    std::string release = "unknown";
};

auto try_load_tool_identity_from_root(const std::filesystem::path& root)
    -> std::optional<ToolIdentity> {
    const auto manifest = core::load_manifest(root / "ppargo.toml");
    if (!manifest || manifest->package.name != "ppargo") {
        return std::nullopt;
    }
    return ToolIdentity{
        .repo_root = root,
        .name = manifest->package.name,
        .release = manifest->package.version,
    };
}

auto try_tool_identity() -> std::optional<ToolIdentity> {
    if (auto cwd_root = core::find_project_root(); cwd_root.has_value()) {
        if (auto identity = try_load_tool_identity_from_root(*cwd_root);
            identity.has_value()) {
            return identity;
        }
    }

    const auto exe = core::current_executable_path();
    if (!exe.has_value()) {
        return std::nullopt;
    }

    if (auto exe_root = core::find_project_root(exe->parent_path());
        exe_root.has_value()) {
        return try_load_tool_identity_from_root(*exe_root);
    }
    return std::nullopt;
}

auto git_output_line(const std::filesystem::path& repo_root,
                     std::span<const std::string> args)
    -> std::optional<std::string> {
    auto result = util::process::run_result("git", args,
                                            capture_output_options(repo_root));
    if (!result || result->exit_code != 0) {
        return std::nullopt;
    }
    const auto line = first_output_line(result->output);
    if (line.empty()) {
        return std::nullopt;
    }
    return line;
}

auto try_git_metadata() -> std::optional<GitMetadata> {
    const auto identity = try_tool_identity();
    if (!identity.has_value()) {
        return std::nullopt;
    }
    const std::vector<std::string> full_hash_args{"rev-parse", "HEAD"};
    const std::vector<std::string> date_args{"show", "-s", "--format=%cI",
                                             "HEAD"};

    auto full_hash = git_output_line(identity->repo_root, full_hash_args);
    auto commit_date = git_output_line(identity->repo_root, date_args);
    if (!full_hash.has_value() || !commit_date.has_value()) {
        return std::nullopt;
    }

    GitMetadata metadata{
        .short_hash =
            full_hash->substr(0, std::min<std::size_t>(9, full_hash->size())),
        .full_hash = *full_hash,
        .commit_date = commit_date->substr(
            0, std::min<std::size_t>(10, commit_date->size())),
    };
    return metadata;
}

auto short_version_line(const std::optional<ToolIdentity>& identity,
                        const std::optional<GitMetadata>& metadata)
    -> std::string {
    const std::string_view name =
        identity.has_value() ? std::string_view(identity->name) : "ppargo";
    const std::string_view release =
        identity.has_value() ? std::string_view(identity->release) : "unknown";
    if (metadata.has_value()) {
        return std::format("{} {} ({} {})", name, release, metadata->short_hash,
                           metadata->commit_date);
    }
    return std::format("{} {}", name, release);
}

}  // namespace

auto VersionCommand::execute() const -> util::Status {
    const auto identity = try_tool_identity();
    const auto compiler_path = std::filesystem::path("clang++");
    const auto compiler_version =
        describe_tool_version(compiler_path, "clang++");
    const auto git_version = describe_tool_version("git", "git");
    const auto curl_version = describe_tool_version("curl.exe", "curl");
    const auto metadata = try_git_metadata();

    util::output::line(util::output::Stream::Stdout,
                       short_version_line(identity, metadata));
    if (verbose) {
        util::output::line(
            util::output::Stream::Stdout,
            std::format("release: {}",
                        identity.has_value() ? identity->release : "unknown"));
        util::output::line(
            util::output::Stream::Stdout,
            std::format("commit-hash: {}", metadata.has_value()
                                               ? metadata->full_hash
                                               : "unknown"));
        util::output::line(
            util::output::Stream::Stdout,
            std::format("commit-date: {}", metadata.has_value()
                                               ? metadata->commit_date
                                               : "unknown"));
        util::output::line(util::output::Stream::Stdout,
                           std::format("host: {}", core::host_triple()));
        util::output::line(util::output::Stream::Stdout,
                           std::format("compiler: {}", compiler_version));
        util::output::line(util::output::Stream::Stdout,
                           std::format("git: {}", git_version));
        util::output::line(util::output::Stream::Stdout,
                           std::format("curl: {}", curl_version));
        util::output::line(util::output::Stream::Stdout,
                           std::format("os: {}", core::os_description()));
    }
    return util::Ok;
}

auto HelpCommand::execute() const -> util::Status {
    util::output::write_help_text(help_text(topic));
    return util::Ok;
}

}  // namespace cli
