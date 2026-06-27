#include "util/process_detail.hpp"

#ifndef _WIN32

#include <array>
#include <chrono>
#include <thread>
#include <vector>

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

class PosixFd {
   public:
    PosixFd() noexcept = default;
    explicit PosixFd(int fd) noexcept : fd_(fd) {}

    PosixFd(const PosixFd&) = delete;
    auto operator=(const PosixFd&) -> PosixFd& = delete;

    PosixFd(PosixFd&& other) noexcept : fd_(other.release()) {}

    auto operator=(PosixFd&& other) noexcept -> PosixFd& {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    ~PosixFd() noexcept { reset(); }

    [[nodiscard]] auto get() const noexcept -> int { return fd_; }
    [[nodiscard]] auto valid() const noexcept -> bool { return fd_ >= 0; }

    void reset(int fd = -1) noexcept {
        if (valid()) {
            close(fd_);
        }
        fd_ = fd;
    }

    [[nodiscard]] auto release() noexcept -> int {
        const int released = fd_;
        fd_ = -1;
        return released;
    }

   private:
    int fd_ = -1;
};

struct PosixPipe {
    PosixFd read;
    PosixFd write;
};

auto build_argv(const std::filesystem::path& program,
                std::vector<std::string>& owned,
                std::span<const std::string> args) -> std::vector<char*> {
    owned.clear();
    owned.reserve(args.size() + 1);
    owned.push_back(program.string());
    for (const auto& arg : args) {
        owned.push_back(arg);
    }

    std::vector<char*> argv;
    argv.reserve(owned.size() + 1);
    for (auto& item : owned) {
        argv.push_back(item.data());
    }
    argv.push_back(nullptr);
    return argv;
}

auto open_dev_null() -> util::Result<PosixFd> {
    const int fd = open("/dev/null", O_RDONLY);
    if (fd < 0) {
        return std::unexpected(
            util::make_error("Failed to open /dev/null for child stdin."));
    }
    return PosixFd(fd);
}

auto set_nonblocking(int fd) -> util::Status {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return std::unexpected(
            util::make_error("Failed to read output pipe flags."));
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return std::unexpected(
            util::make_error("Failed to set output pipe non-blocking mode."));
    }
    return util::Ok;
}

auto drain_pipe(int fd, std::string& output) -> util::Status {
    std::array<char, 4096> buffer{};
    while (true) {
        const auto count = read(fd, buffer.data(), buffer.size());
        if (count > 0) {
            output.append(buffer.data(), static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) {
            return util::Ok;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return util::Ok;
        }
        return std::unexpected(
            util::make_error("Failed to read child output."));
    }
}

}  // namespace

namespace util::process::detail {

auto run_platform_process(const std::filesystem::path& program,
                          std::span<const std::string> args,
                          const RunOptions& options,
                          bool allow_policy_runner_copy,
                          bool policy_fallback_used)
    -> util::Result<RunResult> {
    (void)allow_policy_runner_copy;
    const auto wall_start = std::chrono::steady_clock::now();

    std::vector<std::string> owned;
    auto argv = build_argv(program, owned, args);

    PosixPipe capture_pipe;
    if (options.capture_output) {
        int pipefd[2] = {-1, -1};
        if (pipe(pipefd) != 0) {
            return std::unexpected(
                util::make_error("Failed to create output pipe."));
        }
        capture_pipe.read.reset(pipefd[0]);
        capture_pipe.write.reset(pipefd[1]);
        GUARD(set_nonblocking(capture_pipe.read.get()));
    }

    PosixFd stdin_fd;
    if (options.stdin_mode == StdinMode::Null) {
        auto dev_null = GUARD(open_dev_null());
        stdin_fd = std::move(dev_null);
    }

    const pid_t pid = fork();
    if (pid < 0) {
        return std::unexpected(
            util::make_error("Failed to fork child process."));
    }

    if (pid == 0) {
        if (options.working_directory.has_value()) {
            ::chdir(options.working_directory->c_str());
        }

        if (stdin_fd.valid()) {
            dup2(stdin_fd.get(), STDIN_FILENO);
        }
        if (options.capture_output) {
            dup2(capture_pipe.write.get(), STDOUT_FILENO);
            if (options.merge_stderr) {
                dup2(capture_pipe.write.get(), STDERR_FILENO);
            }
        }

        capture_pipe.read.reset();
        capture_pipe.write.reset();
        stdin_fd.reset();

        execvp(argv[0], argv.data());
        _exit(127);
    }

    capture_pipe.write.reset();
    stdin_fd.reset();

    std::string output;
    int status = 0;
    bool canceled = false;
    while (true) {
        if (options.capture_output && capture_pipe.read.valid()) {
            GUARD(drain_pipe(capture_pipe.read.get(), output));
        }

        const bool use_poll_wait = options.cancel_requested != nullptr ||
                                   options.timeout_ms.has_value();
        const int wait_flags = use_poll_wait ? WNOHANG : 0;
        const auto wait_result = waitpid(pid, &status, wait_flags);
        if (wait_result == pid) {
            break;
        }
        if (wait_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected(
                util::make_error("Failed to wait on child process."));
        }

        if (cancellation_requested(options) ||
            timeout_expired(wall_start, options)) {
            canceled = true;
            kill(pid, SIGTERM);
            while (waitpid(pid, &status, 0) < 0) {
                if (errno != EINTR) {
                    return std::unexpected(util::make_error(
                        "Failed to wait on canceled child process."));
                }
            }
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (options.capture_output && capture_pipe.read.valid()) {
        GUARD(drain_pipe(capture_pipe.read.get(), output));
        capture_pipe.read.reset();
    }

    int exit_code = 1;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    }

    std::optional<ProcessMetrics> metrics;
    if (options.collect_metrics) {
        const auto wall_end = std::chrono::steady_clock::now();
        metrics = ProcessMetrics{
            .wall_ms =
                std::chrono::duration<double, std::milli>(wall_end - wall_start)
                    .count(),
            .peak_working_set_mb = std::nullopt,
            .used_policy_fallback = policy_fallback_used,
        };
    }

    return RunResult{
        .exit_code = exit_code,
        .output = std::move(output),
        .metrics = std::move(metrics),
        .canceled = canceled,
    };
}

}  // namespace util::process::detail

#endif
