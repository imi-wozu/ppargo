#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace core {

struct DependencyArtifacts {
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::filesystem::path> library_directories;
    std::vector<std::string> link_libraries;
    std::vector<std::filesystem::path> runtime_directories;
    std::vector<std::filesystem::path> runtime_files;
};

}  // namespace core
