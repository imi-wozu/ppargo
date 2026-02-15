#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "util/result.hpp"


namespace util::process {

struct RunOptions {
    std::optional<std::filesystem::path> working_directory;
    bool capture_output = false;
    bool merge_stderr = true;
};

struct RunResult {
    int exit_code = -1;
    std::string output;
};

auto run(const std::filesystem::path& program, const std::vector<std::string>& args,
         const RunOptions& options = {}) -> util::Result<RunResult>;
auto run_result(const std::filesystem::path& program,
                const std::vector<std::string>& args,
                const RunOptions& options = {}) -> util::Result<RunResult>;

}  // namespace util::process




