#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace build::source_scan {

enum class SourceKind {
    TranslationUnit,
    ModuleInterfaceUnit,
    ModuleImplementationUnit,
};

struct SourceUnit {
    std::filesystem::path path;
    SourceKind kind{SourceKind::TranslationUnit};
    std::string provides;
    std::vector<std::string> imports;
};

}  // namespace build::source_scan
