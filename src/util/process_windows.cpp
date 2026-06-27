#include "util/process_detail.hpp"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <chrono>
#include <climits>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <format>
#include <memory>
#include <mutex>
#include <ranges>
#include <string>
#include <unordered_set>
#include <vector>

#include <windows.h>
#include <psapi.h>

#include "core/manifest.hpp"
#include "core/paths.hpp"
#include "util/environment.hpp"

namespace {

struct LocalFreeDeleter {
    void operator()(wchar_t* pointer) const noexcept {
        if (pointer != nullptr) {
            LocalFree(pointer);
        }
    }
};

using LocalWideBuffer = std::unique_ptr<wchar_t, LocalFreeDeleter>;

class WinHandle {
   public:
    WinHandle() noexcept = default;
    explicit WinHandle(HANDLE handle) noexcept : handle_(handle) {}

    WinHandle(const WinHandle&) = delete;
    auto operator=(const WinHandle&) -> WinHandle& = delete;

    WinHandle(WinHandle&& other) noexcept : handle_(other.release()) {}

    auto operator=(WinHandle&& other) noexcept -> WinHandle& {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    ~WinHandle() noexcept { reset(); }

    [[nodiscard]] auto get() const noexcept -> HANDLE { return handle_; }

    [[nodiscard]] auto valid() const noexcept -> bool {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    void reset(HANDLE handle = nullptr) noexcept {
        if (valid()) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

    [[nodiscard]] auto release() noexcept -> HANDLE {
        HANDLE released = handle_;
        handle_ = nullptr;
        return released;
    }

   private:
    HANDLE handle_ = nullptr;
};

struct CaptureFile {
    std::filesystem::path path;
    WinHandle handle;
};

auto utf8_to_wide(std::string_view value) -> util::Result<std::wstring> {
    if (value.empty()) {
        return std::wstring();
    }

    const int wide_size = MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (wide_size <= 0) {
        return std::unexpected(
            util::make_error("Failed UTF-8 to UTF-16 conversion."));
    }

    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, value.data(),
                            static_cast<int>(value.size()), wide.data(),
                            wide_size) <= 0) {
        return std::unexpected(
            util::make_error("Failed UTF-8 to UTF-16 conversion."));
    }
    return wide;
}

auto wide_to_utf8(const std::wstring& value) -> std::string {
    if (value.empty()) {
        return std::string();
    }

    const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                                              static_cast<int>(value.size()),
                                              nullptr, 0, nullptr, nullptr);
    if (utf8_size <= 0) {
        return std::string("unknown error");
    }

    std::string utf8(static_cast<std::size_t>(utf8_size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(),
                            static_cast<int>(value.size()), utf8.data(),
                            utf8_size, nullptr, nullptr) <= 0) {
        return std::string("unknown error");
    }
    return utf8;
}

auto windows_error_message(DWORD code) -> std::string {
    wchar_t* raw = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(
        flags, nullptr, code, 0, reinterpret_cast<LPWSTR>(&raw), 0, nullptr);
    LocalWideBuffer buffer(raw);
    if (length == 0 || !buffer) {
        return std::format("Win32 error {}", code);
    }

    std::wstring text(buffer.get(), length);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' ||
                             text.back() == L' ' || text.back() == L'\t')) {
        text.pop_back();
    }

    const auto utf8 = wide_to_utf8(text);
    return utf8.empty() ? std::format("Win32 error {}", code) : utf8;
}

auto is_policy_block_error(DWORD code) -> bool {
    if (code == ERROR_ACCESS_DISABLED_BY_POLICY) {
        return true;
    }
#ifdef ERROR_ACCESS_DISABLED_NO_SAFER_UI_BY_POLICY
    if (code == ERROR_ACCESS_DISABLED_NO_SAFER_UI_BY_POLICY) {
        return true;
    }
#endif

    std::string message = windows_error_message(code);
    std::ranges::transform(message, message.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return message.contains("policy") && message.contains("block");
}

auto quote_windows_arg(const std::wstring& arg) -> std::wstring {
    if (arg.empty()) {
        return std::wstring(L"\"\"");
    }

    bool needs_quotes = false;
    for (wchar_t ch : arg) {
        if (ch == L' ' || ch == L'\t' || ch == L'\"') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return arg;
    }

    std::wstring result;
    result.push_back(L'\"');

    std::size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        if (backslashes > 0) {
            result.append(backslashes, L'\\');
            backslashes = 0;
        }
        result.push_back(ch);
    }

    if (backslashes > 0) {
        result.append(backslashes * 2, L'\\');
    }
    result.push_back(L'\"');
    return result;
}

auto blocked_policy_programs() -> std::unordered_set<std::wstring>& {
    static std::unordered_set<std::wstring> blocked;
    return blocked;
}

auto blocked_policy_mutex() -> std::mutex& {
    static std::mutex mutex;
    return mutex;
}

auto is_policy_blocked(const std::filesystem::path& program) -> bool {
    const std::wstring key = program.wstring();
    std::lock_guard<std::mutex> lock(blocked_policy_mutex());
    return blocked_policy_programs().contains(key);
}

void mark_policy_blocked(const std::filesystem::path& program) {
    const std::wstring key = program.wstring();
    std::lock_guard<std::mutex> lock(blocked_policy_mutex());
    blocked_policy_programs().insert(key);
}

auto copy_file_if_stale(const std::filesystem::path& source,
                        const std::filesystem::path& destination)
    -> util::Status {
    std::error_code error;
    const auto source_time = std::filesystem::last_write_time(source, error);
    if (error) {
        return std::unexpected(
            util::make_error(std::format("Failed to read timestamp for {} ({})",
                                         source.string(), error.message())));
    }

    bool should_copy = true;
    const bool destination_exists =
        std::filesystem::exists(destination, error) && !error;
    if (destination_exists) {
        const auto destination_time =
            std::filesystem::last_write_time(destination, error);
        if (!error && destination_time >= source_time) {
            should_copy = false;
        }
    }
    error.clear();

    if (!should_copy) {
        return util::Ok;
    }

    std::filesystem::copy_file(
        source, destination, std::filesystem::copy_options::overwrite_existing,
        error);
    if (error) {
        return std::unexpected(util::make_error(std::format(
            "Failed to stage runnable copy {} -> {} ({})", source.string(),
            destination.string(), error.message())));
    }
    return util::Ok;
}

auto project_runner_cache_root(const std::filesystem::path& program)
    -> util::Result<std::filesystem::path> {
    auto project_root = core::find_project_root(program.parent_path());
    if (!project_root) {
        return std::unexpected(std::move(project_root.error()));
    }

    auto manifest = core::load_manifest(*project_root / "ppargo.toml");
    if (!manifest) {
        return std::unexpected(std::move(manifest.error()));
    }

    return core::runner_cache_dir(*project_root, *manifest) / "policy";
}

auto make_policy_runner_copy(const std::filesystem::path& program)
    -> util::Result<std::filesystem::path> {
    const auto runner_root = GUARD(project_runner_cache_root(program));

    const auto key = std::to_wstring(
        std::hash<std::wstring>{}(program.parent_path().wstring()));
    const auto runner_dir = runner_root / key;

    std::error_code error;
    std::filesystem::create_directories(runner_dir, error);
    if (error) {
        return std::unexpected(util::make_error(
            std::format("Failed to create policy runner directory {} ({})",
                        runner_dir.string(), error.message())));
    }

    const auto staged_program = runner_dir / program.filename();
    GUARD(copy_file_if_stale(program, staged_program));

    const auto sibling_dir = program.parent_path();
    if (std::filesystem::exists(sibling_dir, error) && !error) {
        for (std::filesystem::directory_iterator it(sibling_dir, error), end;
             it != end; it.increment(error)) {
            if (error) {
                return std::unexpected(util::make_error(std::format(
                    "Failed while staging runtime files from {} ({})",
                    sibling_dir.string(), error.message())));
            }
            if (!it->is_regular_file(error)) {
                error.clear();
                continue;
            }
            auto extension = it->path().extension().wstring();
            std::ranges::transform(
                extension, extension.begin(), [](wchar_t ch) {
                    return static_cast<wchar_t>(std::towlower(ch));
                });
            if (extension != L".dll") {
                continue;
            }
            GUARD(copy_file_if_stale(it->path(),
                                     runner_dir / it->path().filename()));
        }
    }

    return staged_program;
}

auto build_windows_command_line(const std::filesystem::path& program,
                                std::span<const std::string> args)
    -> util::Result<std::wstring> {
    std::wstring command = quote_windows_arg(program.wstring());
    for (const auto& arg : args) {
        command.push_back(L' ');
        auto wide_arg = GUARD(utf8_to_wide(arg));
        command += quote_windows_arg(wide_arg);
    }
    return command;
}

auto is_cmd_program(const std::filesystem::path& program) -> bool {
    std::wstring file = program.filename().wstring();
    std::ranges::transform(file, file.begin(), [](const wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return file == L"cmd" || file == L"cmd.exe";
}

auto build_cmd_fallback_command(const std::filesystem::path& program,
                                std::span<const std::string> args)
    -> util::Result<std::string> {
    std::wstring command = quote_windows_arg(program.wstring());
    for (const auto& arg : args) {
        command.push_back(L' ');
        auto wide_arg = GUARD(utf8_to_wide(arg));
        command += quote_windows_arg(wide_arg);
    }
    return wide_to_utf8(command);
}

auto read_text_file(const std::filesystem::path& path) -> std::string {
    std::string output;
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return output;
    }
    input.seekg(0, std::ios::end);
    output.resize(static_cast<std::size_t>(input.tellg()));
    input.seekg(0, std::ios::beg);
    input.read(output.data(), static_cast<std::streamsize>(output.size()));
    return output;
}

auto null_input_handle() -> util::Result<WinHandle> {
    HANDLE handle =
        CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(
            util::make_error("Failed to open NUL for child stdin."));
    }
    return WinHandle(handle);
}

auto query_peak_working_set_mb(HANDLE process) -> std::optional<double> {
    using GetMemoryInfoFn =
        BOOL(WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);

    auto resolve_fn = []() -> GetMemoryInfoFn {
        if (const auto kernel = GetModuleHandleW(L"kernel32.dll");
            kernel != nullptr) {
            if (const auto symbol =
                    GetProcAddress(kernel, "K32GetProcessMemoryInfo");
                symbol != nullptr) {
                return reinterpret_cast<GetMemoryInfoFn>(symbol);
            }
        }
        if (const auto psapi = LoadLibraryW(L"psapi.dll"); psapi != nullptr) {
            if (const auto symbol =
                    GetProcAddress(psapi, "GetProcessMemoryInfo");
                symbol != nullptr) {
                return reinterpret_cast<GetMemoryInfoFn>(symbol);
            }
        }
        return nullptr;
    };

    static const auto get_memory_info = resolve_fn();
    if (get_memory_info == nullptr) {
        return std::nullopt;
    }

    PROCESS_MEMORY_COUNTERS counters{};
    if (!get_memory_info(process, &counters, sizeof(counters))) {
        return std::nullopt;
    }
    return static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0);
}

auto create_capture_file(const SECURITY_ATTRIBUTES& security_attributes)
    -> util::Result<CaptureFile> {
    std::array<wchar_t, MAX_PATH> temp_dir{};
    const auto temp_dir_len =
        GetTempPathW(static_cast<DWORD>(temp_dir.size()), temp_dir.data());
    if (temp_dir_len == 0 || temp_dir_len >= temp_dir.size()) {
        return std::unexpected(
            util::make_error("Failed to resolve temporary directory for output capture."));
    }

    std::array<wchar_t, MAX_PATH> temp_name{};
    if (GetTempFileNameW(temp_dir.data(), L"ppg", 0, temp_name.data()) == 0) {
        return std::unexpected(
            util::make_error("Failed to allocate temporary file for output capture."));
    }

    HANDLE raw_handle = CreateFileW(
        temp_name.data(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        const_cast<SECURITY_ATTRIBUTES*>(&security_attributes), CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED |
            FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (raw_handle == INVALID_HANDLE_VALUE) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(temp_name.data()), ec);
        return std::unexpected(
            util::make_error("Failed to open temporary file for output capture."));
    }

    return CaptureFile{
        .path = std::filesystem::path(temp_name.data()),
        .handle = WinHandle(raw_handle),
    };
}

}  // namespace

namespace util::process::detail {

auto run_platform_process(const std::filesystem::path& program,
                          std::span<const std::string> args,
                          const RunOptions& options,
                          bool allow_policy_runner_copy,
                          bool policy_fallback_used)
    -> util::Result<RunResult> {
    const auto wall_start = std::chrono::steady_clock::now();

    if (!is_cmd_program(program) && is_policy_blocked(program)) {
        if (allow_policy_runner_copy) {
            auto staged_program = GUARD(make_policy_runner_copy(program));
            return run_platform_process(staged_program, args, options, false, true);
        }
        auto command = GUARD(build_cmd_fallback_command(program, args));
        return run_platform_process(
            std::filesystem::path("cmd.exe"),
            std::vector<std::string>{"/d", "/s", "/c", command}, options, false,
            true);
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};

    CaptureFile capture_file;
    WinHandle stdin_handle;
    WinHandle process_handle;
    WinHandle thread_handle;
    WinHandle job_handle;
    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    bool inherit_handles = false;
    if (options.capture_output) {
        auto created_file = GUARD(create_capture_file(security_attributes));
        capture_file = std::move(created_file);
    }

    if (options.stdin_mode == StdinMode::Null) {
        auto null_input = GUARD(null_input_handle());
        stdin_handle = std::move(null_input);
    }

    if (options.capture_output || options.stdin_mode == StdinMode::Null) {
        startup_info.dwFlags |= STARTF_USESTDHANDLES;
        startup_info.hStdInput = stdin_handle.valid()
                                     ? stdin_handle.get()
                                     : GetStdHandle(STD_INPUT_HANDLE);
        startup_info.hStdOutput = options.capture_output
                                      ? capture_file.handle.get()
                                      : GetStdHandle(STD_OUTPUT_HANDLE);
        startup_info.hStdError = options.capture_output && options.merge_stderr
                                     ? capture_file.handle.get()
                                     : GetStdHandle(STD_ERROR_HANDLE);
        inherit_handles = true;
    }

    auto command_line = GUARD(build_windows_command_line(program, args));
    std::vector<wchar_t> command_buffer(command_line.begin(),
                                        command_line.end());
    command_buffer.push_back(L'\0');

    std::wstring cwd_wide;
    LPCWSTR cwd_ptr = nullptr;
    if (options.working_directory.has_value()) {
        cwd_wide = options.working_directory->wstring();
        cwd_ptr = cwd_wide.c_str();
    }

    DWORD creation_flags = 0;
    if (options.cancel_requested != nullptr) {
        creation_flags |= CREATE_SUSPENDED;
    }

    if (!CreateProcessW(nullptr, command_buffer.data(), nullptr, nullptr,
                        inherit_handles, creation_flags, nullptr, cwd_ptr,
                        &startup_info, &process_info)) {
        const DWORD error = GetLastError();
        if (is_policy_block_error(error) && !is_cmd_program(program)) {
            mark_policy_blocked(program);
            if (allow_policy_runner_copy) {
                auto staged_program = GUARD(make_policy_runner_copy(program));
                return run_platform_process(staged_program, args, options, false,
                                            true);
            }
            auto command = GUARD(build_cmd_fallback_command(program, args));
            return run_platform_process(
                std::filesystem::path("cmd.exe"),
                std::vector<std::string>{"/d", "/s", "/c", command}, options,
                false, true);
        }

        return std::unexpected(util::make_error(
            std::format("Failed to execute command: {} ({})", program.string(),
                        windows_error_message(error))));
    }

    process_handle.reset(process_info.hProcess);
    thread_handle.reset(process_info.hThread);
    if (options.cancel_requested != nullptr) {
        HANDLE raw_job = CreateJobObjectW(nullptr, nullptr);
        if (raw_job != nullptr) {
            job_handle.reset(raw_job);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_info{};
            job_info.BasicLimitInformation.LimitFlags =
                JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job_handle.get(),
                                         JobObjectExtendedLimitInformation,
                                         &job_info, sizeof(job_info)) ||
                !AssignProcessToJobObject(job_handle.get(),
                                          process_handle.get())) {
                job_handle.reset();
            }
        }
        if (ResumeThread(thread_handle.get()) == static_cast<DWORD>(-1)) {
            return std::unexpected(util::make_error(std::format(
                "Failed to resume command: {} ({})", program.string(),
                windows_error_message(GetLastError()))));
        }
    }
    stdin_handle.reset();
    capture_file.handle.reset();

    bool canceled = false;
    while (true) {
        const bool use_poll_wait = options.cancel_requested != nullptr ||
                                   options.timeout_ms.has_value();
        const DWORD wait_timeout = use_poll_wait ? 50 : INFINITE;
        const auto wait_result =
            WaitForSingleObject(process_handle.get(), wait_timeout);
        if (wait_result == WAIT_OBJECT_0) {
            break;
        }
        if (wait_result == WAIT_FAILED) {
            return std::unexpected(util::make_error(std::format(
                "Failed while waiting on command: {} ({})", program.string(),
                windows_error_message(GetLastError()))));
        }
        if (wait_result == WAIT_TIMEOUT) {
            const bool should_cancel =
                cancellation_requested(options) || timeout_expired(wall_start, options);
            if (should_cancel) {
                if (job_handle.valid()) {
                    job_handle.reset();
                } else if (!TerminateProcess(process_handle.get(), 1)) {
                    return std::unexpected(util::make_error(std::format(
                        "Failed to cancel command: {} ({})", program.string(),
                        windows_error_message(GetLastError()))));
                }
                WaitForSingleObject(process_handle.get(), INFINITE);
                canceled = true;
                break;
            }
        }
    }
    const auto wall_end = std::chrono::steady_clock::now();

    DWORD exit_code = 1;
    if (!GetExitCodeProcess(process_handle.get(), &exit_code)) {
        return std::unexpected(util::make_error(
            std::format("Failed to get exit code for: {}", program.string())));
    }

    std::string captured;
    if (options.capture_output) {
        captured = read_text_file(capture_file.path);
        std::error_code ec;
        std::filesystem::remove(capture_file.path, ec);
    }

    std::optional<ProcessMetrics> metrics;
    if (options.collect_metrics) {
        metrics = ProcessMetrics{
            .wall_ms =
                std::chrono::duration<double, std::milli>(wall_end - wall_start)
                    .count(),
            .peak_working_set_mb =
                query_peak_working_set_mb(process_handle.get()),
            .used_policy_fallback = policy_fallback_used,
        };
    }

    if (exit_code > static_cast<DWORD>(INT_MAX)) {
        return std::unexpected(util::make_error(
            std::format("Exit code overflow for: {}", program.string())));
    }

    return RunResult{
        .exit_code = static_cast<int>(exit_code),
        .output = std::move(captured),
        .metrics = std::move(metrics),
        .canceled = canceled,
    };
}

}  // namespace util::process::detail

#endif
