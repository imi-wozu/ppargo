#include "util/process.hpp"

#include <array>
#include <climits>
#include <format>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif


namespace {

#ifdef _WIN32

auto utf8_to_wide(const std::string& value) -> util::Result<std::wstring> {
    if (value.empty()) {
        return std::wstring();
    }

    const int wide_size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                              static_cast<int>(value.size()), nullptr, 0);
    if (wide_size <= 0) {
        return std::unexpected("Process Error: Failed UTF-8 to UTF-16 conversion.");
    }

    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                            wide.data(), wide_size) <= 0) {
        return std::unexpected("Process Error: Failed UTF-8 to UTF-16 conversion.");
    }
    return wide;
}

auto wide_to_utf8(const std::wstring& value) -> std::string {
    if (value.empty()) {
        return std::string();
    }

    const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                                              static_cast<int>(value.size()), nullptr, 0,
                                              nullptr, nullptr);
    if (utf8_size <= 0) {
        return std::string("unknown error");
    }

    std::string utf8(static_cast<std::size_t>(utf8_size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                            utf8.data(), utf8_size, nullptr, nullptr) <= 0) {
        return std::string("unknown error");
    }
    return utf8;
}

auto windows_error_message(DWORD code) -> std::string {
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length =
        FormatMessageW(flags, nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0,
                       nullptr);
    if (length == 0 || buffer == nullptr) {
        return std::format("Win32 error {}", code);
    }

    std::wstring text(buffer, length);
    LocalFree(buffer);

    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' ||
                             text.back() == L' ' || text.back() == L'\t')) {
        text.pop_back();
    }

    const auto utf8 = wide_to_utf8(text);
    return utf8.empty() ? std::format("Win32 error {}", code) : utf8;
}

auto quote_windows_arg(const std::wstring& arg) -> std::wstring {
    if (arg.empty()) {
        return std::wstring(L"\"\"");
    }

    bool needs_quotes = false;
    for (wchar_t ch : arg) {
        if (ch == L' ' || ch == L'\t' || ch == L'"') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return arg;
    }

    std::wstring result;
    result.push_back(L'"');

    std::size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
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
    result.push_back(L'"');
    return result;
}

auto build_windows_command_line(const std::filesystem::path& program,
                                const std::vector<std::string>& args)
    -> util::Result<std::wstring> {
    std::wstring command = quote_windows_arg(program.wstring());
    for (const auto& arg : args) {
        command.push_back(L' ');
        auto wide_arg = TRY(utf8_to_wide(arg));
        command += quote_windows_arg(wide_arg);
    }
    return command;
}

auto read_handle(HANDLE handle) -> std::string {
    std::string output;
    std::array<char, 4096> buffer{};
    DWORD read = 0;
    while (ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) &&
           read > 0) {
        output.append(buffer.data(), read);
    }
    return output;
}

#endif

#ifndef _WIN32
auto build_argv(const std::filesystem::path& program, std::vector<std::string>& owned,
                const std::vector<std::string>& args) -> std::vector<char*> {
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
#endif

auto run_impl(const std::filesystem::path& program, const std::vector<std::string>& args,
              const util::process::RunOptions& options)
    -> util::Result<util::process::RunResult> {
    if (program.empty()) {
        return std::unexpected("Process Error: Program path is empty.");
    }

#ifdef _WIN32
    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    bool inherit_handles = false;
    if (options.capture_output) {
        if (!CreatePipe(&read_pipe, &write_pipe, &security_attributes, 0)) {
            return std::unexpected("Process Error: Failed to create output pipe.");
        }

        if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(read_pipe);
            CloseHandle(write_pipe);
            return std::unexpected(
                "Process Error: Failed to configure output capture pipe.");
        }

        startup_info.dwFlags |= STARTF_USESTDHANDLES;
        startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup_info.hStdOutput = write_pipe;
        startup_info.hStdError = options.merge_stderr ? write_pipe
                                                      : GetStdHandle(STD_ERROR_HANDLE);
        inherit_handles = true;
    }

    auto command_line = TRY(build_windows_command_line(program, args));
    std::vector<wchar_t> command_buffer(command_line.begin(), command_line.end());
    command_buffer.push_back(L'\0');

    std::wstring cwd_wide;
    LPCWSTR cwd_ptr = nullptr;
    if (options.working_directory.has_value()) {
        cwd_wide = options.working_directory->wstring();
        cwd_ptr = cwd_wide.c_str();
    }

    if (!CreateProcessW(nullptr, command_buffer.data(), nullptr, nullptr, inherit_handles,
                        0, nullptr, cwd_ptr, &startup_info, &process_info)) {
        const DWORD err = GetLastError();
        if (options.capture_output) {
            CloseHandle(read_pipe);
            CloseHandle(write_pipe);
        }
        return std::unexpected(std::format(
            "Process Error: Failed to execute command: {} ({})", program.string(),
            windows_error_message(err)));
    }

    if (options.capture_output) {
        CloseHandle(write_pipe);
        write_pipe = nullptr;
    }

    WaitForSingleObject(process_info.hProcess, INFINITE);

    DWORD exit_code = 1;
    if (!GetExitCodeProcess(process_info.hProcess, &exit_code)) {
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        if (options.capture_output && read_pipe != nullptr) {
            CloseHandle(read_pipe);
        }
        return std::unexpected(
            std::format("Process Error: Failed to get exit code for: {}", program.string()));
    }

    std::string captured;
    if (options.capture_output && read_pipe != nullptr) {
        captured = read_handle(read_pipe);
        CloseHandle(read_pipe);
    }

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);

    if (exit_code > static_cast<DWORD>(INT_MAX)) {
        return std::unexpected(
            std::format("Process Error: Exit code overflow for: {}", program.string()));
    }

    return util::process::RunResult{static_cast<int>(exit_code), std::move(captured)};

#else
    std::vector<std::string> owned;
    auto argv = build_argv(program, owned, args);

    int pipefd[2] = {-1, -1};
    if (options.capture_output && pipe(pipefd) != 0) {
        return std::unexpected("Process Error: Failed to create output pipe.");
    }

    const pid_t pid = fork();
    if (pid < 0) {
        if (options.capture_output) {
            close(pipefd[0]);
            close(pipefd[1]);
        }
        return std::unexpected("Process Error: Failed to fork child process.");
    }

    if (pid == 0) {
        if (options.working_directory.has_value()) {
            ::chdir(options.working_directory->c_str());
        }

        if (options.capture_output) {
            dup2(pipefd[1], STDOUT_FILENO);
            if (options.merge_stderr) {
                dup2(pipefd[1], STDERR_FILENO);
            }
            close(pipefd[0]);
            close(pipefd[1]);
        }

        execvp(argv[0], argv.data());
        _exit(127);
    }

    if (options.capture_output) {
        close(pipefd[1]);
    }

    std::string output;
    if (options.capture_output) {
        std::array<char, 4096> buffer{};
        ssize_t count = 0;
        while ((count = read(pipefd[0], buffer.data(), buffer.size())) > 0) {
            output.append(buffer.data(), static_cast<std::size_t>(count));
        }
        close(pipefd[0]);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return std::unexpected("Process Error: Failed to wait on child process.");
    }

    int exit_code = 1;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    }

    return util::process::RunResult{exit_code, std::move(output)};
#endif
}

}  // namespace

namespace util::process {

auto run(const std::filesystem::path& program, const std::vector<std::string>& args,
         const RunOptions& options) -> util::Result<RunResult> {
    return run_impl(program, args, options);
}

auto run_result(const std::filesystem::path& program,
                const std::vector<std::string>& args,
                const RunOptions& options) -> util::Result<RunResult> {
    return run_impl(program, args, options);
}

}  // namespace util::process




