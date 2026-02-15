#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/manifest.hpp"


namespace {

auto write_temp_manifest(const std::string& content) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                         ("ppargo_manifest_test_" + std::to_string(stamp));
    std::filesystem::create_directories(dir);

    const std::filesystem::path manifest = dir / "ppargo.toml";
    std::ofstream out(manifest, std::ios::trunc);
    out << content;
    return manifest;
}

}  // namespace

TEST_CASE("Manifest writer emits canonical top-level sections") {
    auto manifest = core::default_manifest("demo");
    manifest.package.version = "0.2.0";
    manifest.package.edition = "cpp23";
    manifest.dependencies["zlib"] = "1.2.13";
    manifest.features.package_manager = "vcpkg";
    manifest.features.has_vcpkg_root = true;
    manifest.features.vcpkg_root = "C:/vcpkg";
    manifest.build.binary_name = "argo";

    const auto toml = core::to_toml(manifest);
    REQUIRE(toml.has_value());

    REQUIRE(toml->find("[package]\n") != std::string::npos);
    REQUIRE(toml->find("[toolchain]\n") != std::string::npos);
    REQUIRE(toml->find("[features]\n") != std::string::npos);
    REQUIRE(toml->find("[build]\n") != std::string::npos);
    REQUIRE(toml->find("package_manager = \"vcpkg\"") != std::string::npos);
    REQUIRE(toml->find("[package.metadata.ppargo") == std::string::npos);
}

TEST_CASE("Manifest loader reads legacy metadata style") {
    const std::string legacy = R"([package]
name = "legacy"
version = "0.1.0"
edition = "cpp20"

[dependencies]
zlib = "1.2.13"

[package.metadata.ppargo.toolchain]
compiler = "clang++"
linker = "lld"

[package.metadata.ppargo.features]
packages = true
package_manager = "vcpkg"
vcpkg_root = "C:\\vcpkg"

[package.metadata.ppargo.build]
source-dir = "src"
include-dirs = ["src", "include"]
exclude = ["target/**"]
output-dir = "target/cpp"
binary-name = "legacy"
)";

    const auto manifest_path = write_temp_manifest(legacy);
    const auto manifest = core::load_manifest(manifest_path);
    REQUIRE(manifest.has_value());

    REQUIRE(manifest->package.name == "legacy");
    REQUIRE(manifest->dependencies.count("zlib") == 1);
    REQUIRE(manifest->toolchain.compiler == "clang++");
    REQUIRE(manifest->features.packages);
    REQUIRE(manifest->features.package_manager == "vcpkg");
    REQUIRE(manifest->features.has_vcpkg_root);
    REQUIRE(manifest->features.vcpkg_root.generic_string() == "C:/vcpkg");
    REQUIRE(manifest->build.binary_name == "legacy");

    std::filesystem::remove_all(manifest_path.parent_path());
}

TEST_CASE("Manifest loader rejects duplicate keys") {
    const std::string manifest_text = R"([package]
name = "dup"
name = "dup2"
version = "0.1.0"
edition = "cpp23"
)";

    const auto manifest_path = write_temp_manifest(manifest_text);
    const auto result = core::load_manifest(manifest_path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().find("Duplicate key") != std::string::npos);
    std::filesystem::remove_all(manifest_path.parent_path());
}

TEST_CASE("Manifest loader rejects malformed arrays") {
    const std::string manifest_text = R"([package]
name = "broken"
version = "0.1.0"
edition = "cpp23"

[build]
include_dirs = ["src", "include]
)";

    const auto manifest_path = write_temp_manifest(manifest_text);
    const auto result = core::load_manifest(manifest_path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().find("Invalid TOML array value") != std::string::npos);
    std::filesystem::remove_all(manifest_path.parent_path());
}

TEST_CASE("Manifest loader rejects non-string array elements") {
    const std::string manifest_text = R"([package]
name = "broken"
version = "0.1.0"
edition = "cpp23"

[build]
include_dirs = ["src", 123]
)";

    const auto manifest_path = write_temp_manifest(manifest_text);
    const auto result = core::load_manifest(manifest_path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().find("non-string element") != std::string::npos);
    std::filesystem::remove_all(manifest_path.parent_path());
}

TEST_CASE("Manifest canonical roundtrip remains stable") {
    auto manifest = core::default_manifest("roundtrip");
    manifest.package.version = "1.2.3";
    manifest.dependencies["zlib"] = "1.3.1";
    manifest.build.binary_name = "roundtrip";

    const auto toml = core::to_toml(manifest);
    REQUIRE(toml.has_value());
    const auto manifest_path = write_temp_manifest(*toml);
    const auto loaded = core::load_manifest(manifest_path);
    REQUIRE(loaded.has_value());
    const auto emitted = core::to_toml(*loaded);
    REQUIRE(emitted.has_value());

    REQUIRE(*emitted == *toml);
    std::filesystem::remove_all(manifest_path.parent_path());
}

TEST_CASE("Manifest loader accepts UTF-8 BOM") {
    const std::string bom_manifest =
        "\xEF\xBB\xBF[package]\n"
        "name = \"bom\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"cpp23\"\n";

    const auto manifest_path = write_temp_manifest(bom_manifest);
    const auto manifest = core::load_manifest(manifest_path);
    REQUIRE(manifest.has_value());
    REQUIRE(manifest->package.name == "bom");
    std::filesystem::remove_all(manifest_path.parent_path());
}


