#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "build/fingerprint.hpp"


namespace {

auto make_temp_dir() -> std::filesystem::path {
    const auto stamp =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto dir = std::filesystem::temp_directory_path() / ("ppargo_fingerprint_test_" + stamp);
    std::filesystem::create_directories(dir);
    return dir;
}

auto write_text(const std::filesystem::path& path, const std::string& content) -> void {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    out << content;
}

auto set_time(const std::filesystem::path& path, std::filesystem::file_time_type time) -> void {
    std::error_code ec;
    std::filesystem::last_write_time(path, time, ec);
}

}  // namespace

TEST_CASE("fingerprint detects missing object") {
    const auto root = make_temp_dir();
    const auto source_root = root / "src";
    const auto source = source_root / "main.cpp";
    const auto dep = root / "target/main.d";

    write_text(source, "int main() { return 0; }\n");
    write_text(dep, "target/main.o: src/main.cpp\n");

    const auto decision =
        build::fingerprint::evaluate_rebuild(source_root, source, root / "target/main.o", dep);
    REQUIRE(decision.reason == build::fingerprint::RebuildReason::MissingObject);

    std::filesystem::remove_all(root);
}

TEST_CASE("fingerprint detects missing depfile") {
    const auto root = make_temp_dir();
    const auto source_root = root / "src";
    const auto source = source_root / "main.cpp";
    const auto object = root / "target/main.o";

    write_text(source, "int main() { return 0; }\n");
    write_text(object, "obj");

    const auto decision = build::fingerprint::evaluate_rebuild(source_root, source, object,
                                                               root / "target/main.d");
    REQUIRE(decision.reason == build::fingerprint::RebuildReason::MissingDepFile);

    std::filesystem::remove_all(root);
}

TEST_CASE("fingerprint detects source newer than object") {
    const auto root = make_temp_dir();
    const auto source_root = root / "src";
    const auto source = source_root / "main.cpp";
    const auto object = root / "target/main.o";
    const auto dep = root / "target/main.d";

    write_text(source, "int main() { return 0; }\n");
    write_text(object, "obj");
    write_text(dep, "target/main.o: src/main.cpp\n");

    const auto now = std::filesystem::file_time_type::clock::now();
    set_time(object, now);
    set_time(source, now + std::chrono::seconds(5));

    const auto decision =
        build::fingerprint::evaluate_rebuild(source_root, source, object, dep);
    REQUIRE(decision.reason == build::fingerprint::RebuildReason::SourceNewer);

    std::filesystem::remove_all(root);
}

TEST_CASE("fingerprint detects missing dependency listed in depfile") {
    const auto root = make_temp_dir();
    const auto source_root = root / "src";
    const auto source = source_root / "main.cpp";
    const auto object = root / "target/main.o";
    const auto dep = root / "target/main.d";

    write_text(source, "int main() { return 0; }\n");
    write_text(object, "obj");
    write_text(dep, "target/main.o: src/main.cpp include/missing.hpp\n");

    const auto now = std::filesystem::file_time_type::clock::now();
    set_time(source, now);
    set_time(object, now + std::chrono::seconds(10));

    const auto decision =
        build::fingerprint::evaluate_rebuild(source_root, source, object, dep);
    REQUIRE(decision.reason ==
            build::fingerprint::RebuildReason::DependencyMissing);

    std::filesystem::remove_all(root);
}

TEST_CASE("fingerprint reports up-to-date state when inputs are unchanged") {
    const auto root = make_temp_dir();
    const auto source_root = root / "src";
    const auto source = source_root / "main.cpp";
    const auto header = root / "include/msg.hpp";
    const auto object = root / "target/main.o";
    const auto dep = root / "target/main.d";

    write_text(source, "#include \"msg.hpp\"\nint main() { return 0; }\n");
    write_text(header, "#pragma once\n");
    write_text(object, "obj");
    write_text(dep, "target/main.o: src/main.cpp include/msg.hpp\n");

    const auto now = std::filesystem::file_time_type::clock::now();
    set_time(source, now);
    set_time(header, now);
    set_time(object, now + std::chrono::seconds(10));

    const auto decision =
        build::fingerprint::evaluate_rebuild(source_root, source, object, dep);
    REQUIRE(decision.reason == build::fingerprint::RebuildReason::UpToDate);

    std::filesystem::remove_all(root);
}



