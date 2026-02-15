#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "util/process.hpp"


namespace {

auto temp_test_dir(const std::string& prefix) {
    const auto stamp =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto dir = std::filesystem::temp_directory_path() / (prefix + "_" + stamp);
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

TEST_CASE("Process runner captures command output") {
#ifdef _WIN32
    util::process::RunOptions options;
    options.capture_output = true;
    options.merge_stderr = true;

    const auto result =
        util::process::run("cmd", {"/d", "/s", "/c", "echo hello"}, options);
    REQUIRE(result.has_value());
    REQUIRE(result->exit_code == 0);
    REQUIRE(result->output.find("hello") != std::string::npos);
#else
    SUCCEED();
#endif
}

TEST_CASE("Process runner preserves spaced arguments") {
#ifdef _WIN32
    const auto dir = temp_test_dir("ppargo_process_test");
    const auto script = dir / "argcheck.cmd";
    {
        std::ofstream out(script, std::ios::trunc);
        out << "@echo off\n";
        out << "if \"%~1\"==\"hello world\" (\n";
        out << "  echo ok\n";
        out << "  exit /b 0\n";
        out << ")\n";
        out << "echo bad:%~1\n";
        out << "exit /b 3\n";
    }

    util::process::RunOptions options;
    options.capture_output = true;
    options.merge_stderr = true;
    options.working_directory = dir;

    const auto result = util::process::run(script, {"hello world"}, options);
    REQUIRE(result.has_value());
    REQUIRE(result->exit_code == 0);
    REQUIRE(result->output.find("ok") != std::string::npos);

    std::filesystem::remove_all(dir);
#else
    SUCCEED();
#endif
}


