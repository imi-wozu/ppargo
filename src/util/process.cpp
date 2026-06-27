#include "util/process.hpp"

#include "util/process_detail.hpp"

namespace util::process {

auto run(const std::filesystem::path& program,
         std::span<const std::string> args, const RunOptions& options)
    -> util::Result<RunResult> {
    return detail::run_platform_process(program, args, options, true, false);
}

auto run_result(const std::filesystem::path& program,
                std::span<const std::string> args, const RunOptions& options)
    -> util::Result<RunResult> {
    return detail::run_platform_process(program, args, options, true, false);
}

}  // namespace util::process
