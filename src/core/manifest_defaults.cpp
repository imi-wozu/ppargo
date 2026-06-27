#include "core/manifest.hpp"

#include <filesystem>
#include <string>
#include <string_view>

#include "util/environment.hpp"

namespace core {

auto default_manifest(std::string_view  package_name) -> Manifest {
    Manifest manifest;
    manifest.package.name = package_name;
    manifest.package.version = "0.1.0";
    manifest.package.edition = "cpp23";

    manifest.toolchain.compiler = "clang++";

    manifest.features.packages = false;
    manifest.features.package_manager = "vcpkg";

    manifest.build.source_dir = "src";
    manifest.build.include_dirs = {"src", "include"};
    manifest.build.exclude = {"target/**"};
    manifest.build.output_dir = "target/cpp";
    manifest.build.binary_name.clear();
    manifest.build.modules = false;
    manifest.build.module_interface_exts = {".cppm", ".ixx", ".mpp"};
    manifest.build.module_output_dir = "target/cpp/modules";
    manifest.build.aggressive_tu_threshold = 24;
    manifest.build.aggressive_stale_threshold = 8;
    manifest.build.pch_scan_lines = 200;
    manifest.build.pch_frequency_threshold = 0.60;
    manifest.build.pch_max_headers = 40;
    manifest.build.depscan_timeout_ms = 10000;

    if (const auto env_vcpkg_root = util::env::get_path("VCPKG_ROOT");
        env_vcpkg_root.has_value()) {
        manifest.features.vcpkg_root = *env_vcpkg_root;
    } else {
#ifdef _WIN32
        manifest.features.vcpkg_root = std::filesystem::path("C:/vcpkg");
#else
        manifest.features.vcpkg_root = std::filesystem::path("/usr/local/vcpkg");
#endif
    }

    return manifest;
}

}  // namespace core
