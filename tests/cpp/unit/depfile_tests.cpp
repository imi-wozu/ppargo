#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "build/depfile.hpp"


namespace {

auto make_temp_dir() -> std::filesystem::path {
    const auto stamp =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto dir = std::filesystem::temp_directory_path() / ("ppargo_depfile_test_" + stamp);
    std::filesystem::create_directories(dir);
    return dir;
}

auto write_text(const std::filesystem::path& path, const std::string& content) -> void {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    out << content;
}

}  // namespace

TEST_CASE("depfile parser handles simple dependencies") {
    const auto dir = make_temp_dir();
    const auto dep = dir / "main.d";
    write_text(dep, "obj/main.o: src/main.cpp include/msg.hpp\n");

    const auto parsed = build::depfile::parse_dependencies(dep);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->size() == 2);
    REQUIRE((*parsed)[0].generic_string() == "src/main.cpp");
    REQUIRE((*parsed)[1].generic_string() == "include/msg.hpp");

    std::filesystem::remove_all(dir);
}

TEST_CASE("depfile parser handles escaped spaces") {
    const auto dir = make_temp_dir();
    const auto dep = dir / "main.d";
    write_text(dep, "obj/main.o: src/main.cpp include/my\\ header.hpp\n");

    const auto parsed = build::depfile::parse_dependencies(dep);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->size() == 2);
    REQUIRE((*parsed)[1].generic_string() == "include/my header.hpp");

    std::filesystem::remove_all(dir);
}

TEST_CASE("depfile parser handles line continuations and drive paths") {
    const auto dir = make_temp_dir();
    const auto dep = dir / "main.d";
    write_text(dep,
               "obj/main.o: C:/repo/src/main.cpp \\\n"
               " C:/repo/include/a.hpp \\\n"
               " C:/repo/include/b.hpp\n");

    const auto parsed = build::depfile::parse_dependencies(dep);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->size() == 3);
    REQUIRE((*parsed)[0].generic_string() == "C:/repo/src/main.cpp");
    REQUIRE((*parsed)[1].generic_string() == "C:/repo/include/a.hpp");
    REQUIRE((*parsed)[2].generic_string() == "C:/repo/include/b.hpp");

    std::filesystem::remove_all(dir);
}

TEST_CASE("depfile parser preserves windows backslash paths") {
    const auto dir = make_temp_dir();
    const auto dep = dir / "main.d";
    write_text(dep,
               "obj\\main.o: C:\\repo\\src\\main.cpp "
               "C:\\repo\\my\\ header\\message.hpp\n");

    const auto parsed = build::depfile::parse_dependencies(dep);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->size() == 2);
    REQUIRE((*parsed)[0].generic_string() == "C:/repo/src/main.cpp");
    REQUIRE((*parsed)[1].generic_string() ==
            "C:/repo/my header/message.hpp");

    std::filesystem::remove_all(dir);
}


