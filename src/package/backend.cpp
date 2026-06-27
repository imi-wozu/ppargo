#include "package/backend.hpp"

#include "package/backends/ppargo_backend.hpp"
#include "package/backends/vcpkg_backend.hpp"
#include "util/result.hpp"

#include <string_view>

namespace {

enum class BackendKind {
    Vcpkg,
    Ppargo,
};

auto resolve_backend_kind(const core::Manifest& manifest)
    -> util::Result<BackendKind> {
    if (manifest.features.package_manager == "vcpkg") {
        return BackendKind::Vcpkg;
    }
    if (manifest.features.package_manager == "ppargo") {
        return BackendKind::Ppargo;
    }
    return std::unexpected(util::make_error("Unsupported package manager: " +
                           manifest.features.package_manager));
}

template <typename VcpkgFn, typename PpargoFn>
auto dispatch_backend(const core::Manifest& manifest, VcpkgFn&& vcpkg_fn,
                      PpargoFn&& ppargo_fn) -> decltype(vcpkg_fn()) {
    const auto backend = GUARD(resolve_backend_kind(manifest));
    switch (backend) {
        case BackendKind::Vcpkg: {
            return std::forward<VcpkgFn>(vcpkg_fn)();
        }
        case BackendKind::Ppargo: {
            return std::forward<PpargoFn>(ppargo_fn)();
        }
    }
    return std::unexpected(util::make_error("unknown package backend."));
}

}  // namespace

namespace package {

auto backend_name(const core::Manifest& manifest) -> util::Result<std::string> {
    return dispatch_backend(
        manifest, [] { return util::Result<std::string>{std::string("vcpkg")}; },
        [] { return util::Result<std::string>{std::string("ppargo")}; });
}

auto resolve(const std::filesystem::path& project_root,
             const core::Manifest& manifest,
             const FeatureOptions& features) -> util::Result<ResolvedGraph> {
    return dispatch_backend(
        manifest,
        [&] {
            return backends::vcpkg_backend::resolve(project_root, manifest,
                                                    features);
        },
        [&] {
            return backends::ppargo_backend::resolve(project_root, manifest,
                                                     features);
        });
}

auto fetch(const std::filesystem::path& project_root,
           const core::Manifest& manifest,
           const ResolvedGraph& graph) -> util::Status {
    return dispatch_backend(
        manifest,
        [&] { return backends::vcpkg_backend::fetch(project_root, manifest, graph); },
        [&] { return backends::ppargo_backend::fetch(project_root, manifest, graph); });
}

auto install(const std::filesystem::path& project_root,
             const core::Manifest& manifest,
             const ResolvedGraph& graph) -> util::Status {
    return dispatch_backend(
        manifest,
        [&] { return backends::vcpkg_backend::install(project_root, manifest, graph); },
        [&] { return backends::ppargo_backend::install(project_root, manifest, graph); });
}

auto publish(const std::filesystem::path& project_root,
             const core::Manifest& manifest) -> util::Status {
    return dispatch_backend(
        manifest,
        [&] { return backends::vcpkg_backend::publish(project_root, manifest); },
        [&] { return backends::ppargo_backend::publish(project_root, manifest); });
}

auto yank(const std::filesystem::path& project_root,
          const core::Manifest& manifest,
          const YankRequest& request) -> util::Status {
    return dispatch_backend(
        manifest,
        [&] { return backends::vcpkg_backend::yank(project_root, manifest, request); },
        [&] { return backends::ppargo_backend::yank(project_root, manifest, request); });
}

auto owner_add(const std::filesystem::path& project_root,
               const core::Manifest& manifest,
               std::string_view  owner) -> util::Status {
    return dispatch_backend(
        manifest, [&] { return backends::vcpkg_backend::owner_add(project_root, manifest, owner); },
        [&] { return backends::ppargo_backend::owner_add(project_root, manifest, owner); });
}

auto owner_remove(const std::filesystem::path& project_root,
                  const core::Manifest& manifest,
                  std::string_view  owner) -> util::Status {
    return dispatch_backend(
        manifest, [&] { return backends::vcpkg_backend::owner_remove(project_root, manifest, owner); },
        [&] { return backends::ppargo_backend::owner_remove(project_root, manifest, owner); });
}

auto auth_login(const std::filesystem::path& project_root,
                const core::Manifest& manifest,
                std::string_view  registry,
                std::string_view  token) -> util::Status {
    return dispatch_backend(
        manifest,
        [&] { return backends::vcpkg_backend::auth_login(project_root, manifest, registry, token); },
        [&] { return backends::ppargo_backend::auth_login(project_root, manifest, registry, token); });
}

auto auth_logout(const std::filesystem::path& project_root,
                 const core::Manifest& manifest,
                 std::string_view  registry) -> util::Status {
    return dispatch_backend(
        manifest,
        [&] { return backends::vcpkg_backend::auth_logout(project_root, manifest, registry); },
        [&] { return backends::ppargo_backend::auth_logout(project_root, manifest, registry); });
}

}  // namespace package



