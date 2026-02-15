#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <string>
#include <variant>
#include <vector>

#include "cli/parser.hpp"

namespace {

auto parse_args(std::initializer_list<std::string> values)
    -> util::Result<cli::ParsedCommand> {
    std::vector<std::string> args(values);
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    return cli::parse(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

TEST_CASE("CLI parser handles run --release") {
    const auto parsed = parse_args({"argo", "run", "--release"});
    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<cli::RunCommand>(*parsed));
    REQUIRE(std::get<cli::RunCommand>(*parsed).release);
}

TEST_CASE("CLI parser handles run alias r --release") {
    const auto parsed = parse_args({"argo", "r", "--release"});
    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<cli::RunCommand>(*parsed));
    REQUIRE(std::get<cli::RunCommand>(*parsed).release);
}

TEST_CASE("CLI parser handles check --release") {
    const auto parsed = parse_args({"argo", "check", "--release"});
    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<cli::CheckCommand>(*parsed));
    REQUIRE(std::get<cli::CheckCommand>(*parsed).release);
}

TEST_CASE("CLI parser handles check -r") {
    const auto parsed = parse_args({"argo", "check", "-r"});
    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<cli::CheckCommand>(*parsed));
    REQUIRE(std::get<cli::CheckCommand>(*parsed).release);
}

TEST_CASE("CLI parser handles check alias c --release") {
    const auto parsed = parse_args({"argo", "c", "--release"});
    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<cli::CheckCommand>(*parsed));
    REQUIRE(std::get<cli::CheckCommand>(*parsed).release);
}

TEST_CASE("CLI parser handles add command") {
    const auto parsed = parse_args({"argo", "add", "zlib"});
    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<cli::AddCommand>(*parsed));
    REQUIRE(std::get<cli::AddCommand>(*parsed).package == "zlib");
}

TEST_CASE("CLI parser handles build alias b -r") {
    const auto parsed = parse_args({"argo", "b", "-r"});
    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<cli::BuildCommand>(*parsed));
    REQUIRE(std::get<cli::BuildCommand>(*parsed).release);
}

TEST_CASE("CLI parser formats missing new argument like cargo") {
    const auto parsed = parse_args({"argo", "new"});
    REQUIRE_FALSE(parsed.has_value());
    REQUIRE(parsed.error().find(
                "the following required arguments were not provided:") !=
            std::string::npos);
    REQUIRE(parsed.error().find("<PATH>") != std::string::npos);
    REQUIRE(parsed.error().find("Usage: argo new <PATH>") != std::string::npos);
    REQUIRE(parsed.error().find("For more information, try '--help'.") !=
            std::string::npos);
}

TEST_CASE("CLI parser formats missing add argument like cargo") {
    const auto parsed = parse_args({"argo", "add"});
    REQUIRE_FALSE(parsed.has_value());
    REQUIRE(parsed.error().find(
                "the following required arguments were not provided:") !=
            std::string::npos);
    REQUIRE(parsed.error().find("<PACKAGE>") != std::string::npos);
    REQUIRE(parsed.error().find("Usage: argo add <PACKAGE>") != std::string::npos);
    REQUIRE(parsed.error().find("For more information, try '--help'.") !=
            std::string::npos);
}

TEST_CASE("CLI parser rejects unknown command") {
    const auto parsed = parse_args({"argo", "unknown"});
    REQUIRE_FALSE(parsed.has_value());
    REQUIRE(parsed.error().find("no such command: `unknown`") != std::string::npos);
    REQUIRE(parsed.error().find("Usage: argo <COMMAND>") != std::string::npos);
    REQUIRE(parsed.error().find("For more information, try '--help'.") !=
            std::string::npos);
}
