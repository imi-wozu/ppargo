#include "core/system.hpp"

#include <format>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winreg.h>
#include <winternl.h>
#pragma comment(lib, "Advapi32.lib")
#endif

namespace core {

#ifdef _WIN32
namespace {

struct WindowsOsInfo {
    std::string product_name = "Windows";
    std::string version = "unknown";
    std::string architecture = "unknown";
    DWORD build_number = 0;
};

auto read_registry_string(HKEY root, std::wstring_view sub_key,
                          std::wstring_view value_name)
    -> std::optional<std::wstring> {
    wchar_t buffer[256];
    DWORD size = sizeof(buffer);
    const auto status = RegGetValueW(root, std::wstring(sub_key).c_str(),
                                     std::wstring(value_name).c_str(),
                                     RRF_RT_REG_SZ, nullptr, buffer, &size);
    if (status != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return std::wstring(buffer);
}

auto normalize_windows_product_name(std::string product_name, DWORD build_number)
    -> std::string {
    if (build_number < 22000) {
        return product_name;
    }

    constexpr std::string_view kWindows10 = "Windows 10";
    if (const auto pos = product_name.find(kWindows10);
        pos != std::string::npos) {
        product_name.replace(pos, kWindows10.size(), "Windows 11");
        return product_name;
    }
    if (product_name == "Windows") {
        return "Windows 11";
    }
    return product_name;
}

auto windows_os_info() -> WindowsOsInfo {
    WindowsOsInfo info;

    if (const auto product = read_registry_string(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName");
        product.has_value()) {
        info.product_name = std::string(product->begin(), product->end());
    }

    using RtlGetVersionPtr = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    if (const auto ntdll = GetModuleHandleW(L"ntdll.dll"); ntdll != nullptr) {
        const auto rtl_get_version = reinterpret_cast<RtlGetVersionPtr>(
            GetProcAddress(ntdll, "RtlGetVersion"));
        if (rtl_get_version != nullptr) {
            RTL_OSVERSIONINFOW version_info{};
            version_info.dwOSVersionInfoSize = sizeof(version_info);
            if (rtl_get_version(&version_info) == 0) {
                info.build_number = version_info.dwBuildNumber;
                info.version = std::format(
                    "{}.{}.{}", version_info.dwMajorVersion,
                    version_info.dwMinorVersion, version_info.dwBuildNumber);
            }
        }
    }

    SYSTEM_INFO system_info{};
    GetNativeSystemInfo(&system_info);
    switch (system_info.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64:
            info.architecture = "64-bit";
            break;
        case PROCESSOR_ARCHITECTURE_ARM64:
            info.architecture = "64-bit";
            break;
        case PROCESSOR_ARCHITECTURE_INTEL:
            info.architecture = "32-bit";
            break;
        default:
            info.architecture = "unknown";
            break;
    }

    info.product_name =
        normalize_windows_product_name(std::move(info.product_name),
                                       info.build_number);
    return info;
}

}  // namespace
#endif

auto current_executable_path() -> std::optional<std::filesystem::path> {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD written = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return std::nullopt;
        }
        if (written < buffer.size() - 1) {
            buffer.resize(written);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    return std::nullopt;
#endif
}

auto host_triple() -> std::string {
#if defined(_M_ARM64) || defined(__aarch64__)
    constexpr auto arch = "aarch64";
#elif defined(_M_X64) || defined(__x86_64__)
    constexpr auto arch = "x86_64";
#else
    constexpr auto arch = "unknown";
#endif

#if defined(_WIN32)
    constexpr auto system = "pc-windows-msvc";
#elif defined(__APPLE__)
    constexpr auto system = "apple-darwin";
#else
    constexpr auto system = "unknown-linux-gnu";
#endif

    return std::format("{}-{}", arch, system);
}

auto os_description() -> std::string {
#ifdef _WIN32
    const auto info = windows_os_info();
    return std::format("Windows {} ({}) [{}]", info.version, info.product_name,
                       info.architecture);
#else
    return "unknown";
#endif
}

}  // namespace core
