#include "package/resolver/resolver.hpp"
#include "package/backend.hpp"
#include "registry/reference.hpp"
#include "util/result.hpp"



















namespace package::resolver {

auto resolve(const std::filesystem::path& project_root,
             const core::Manifest& manifest,
             const FeatureOptions& features) -> util::Result<ResolvedGraph> {
    auto graph = GUARD(package::resolve(project_root, manifest, features));
    std::sort(graph.packages.begin(), graph.packages.end(),
              [](const ResolvedPackage& lhs, const ResolvedPackage& rhs) {
        if (lhs.name == rhs.name) {
            return lhs.version.compare(rhs.version) < 0;
        }
        return lhs.name.compare(rhs.name) < 0;
    });
    return graph;
}

}  // namespace package::resolver




