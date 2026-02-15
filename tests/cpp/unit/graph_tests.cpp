#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "build/graph.hpp"


TEST_CASE("Exclude patterns match expected paths") {
    const std::vector<std::string> excludes = {"generated/**", "target/**"};

    REQUIRE(build::graph::matches_excludes("generated/tmp/file.cpp", excludes));
    REQUIRE(build::graph::matches_excludes("target/cpp/debug/argo.exe", excludes));
    REQUIRE_FALSE(build::graph::matches_excludes("src/main.cpp", excludes));
}

TEST_CASE("Compiled exclude cache matches helper behavior") {
    const std::vector<std::string> excludes = {"generated/**", "target/**", "**/*.tmp"};
    const auto compiled = build::graph::compile_excludes(excludes);

    const std::vector<std::string> paths = {
        "generated/tmp/file.cpp",
        "target/cpp/debug/argo.exe",
        "src/main.cpp",
        "src/cache/file.tmp"};

    for (const auto& path : paths) {
        REQUIRE(build::graph::matches_excludes(path, compiled) ==
                build::graph::matches_excludes(path, excludes));
    }
}

TEST_CASE("Object path mapping remains source-root relative") {
    const std::filesystem::path source_root = "C:/repo/src";
    const std::filesystem::path object_root = "C:/repo/target/cpp/debug/obj";
    const std::filesystem::path source_file = "C:/repo/src/cli/parser.cpp";

    const auto object_file =
        build::graph::object_path_for_source(source_root, object_root, source_file);
    REQUIRE(object_file.has_value());
    REQUIRE(object_file->generic_string() ==
            "C:/repo/target/cpp/debug/obj/cli/parser.o");
}



