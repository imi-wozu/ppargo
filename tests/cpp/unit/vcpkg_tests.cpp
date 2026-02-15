#include <catch2/catch_test_macros.hpp>

#include <string>

#include "package/vcpkg.hpp"

TEST_CASE("vcpkg search parser extracts package rows") {
    const std::string output = R"(The result may be outdated. Run `git pull` to update.

zlib 1.3.1#1 A compression library
abseil 20240116.0#2 C++ common libraries
line-without-version should-be-ignored
)";

    const auto packages = package::vcpkg::parse_package_list(output);
    REQUIRE(packages.has_value());
    REQUIRE(packages->size() == 2);
    REQUIRE((*packages)[0].name == "zlib");
    REQUIRE((*packages)[0].version == "1.3.1#1");
    REQUIRE((*packages)[1].name == "abseil");
}
