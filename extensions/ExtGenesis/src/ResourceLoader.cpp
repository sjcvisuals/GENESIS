#include "ResourceLoader.h"

#include "runtime/RuntimePaths.hpp"

namespace rxext::genesis_ext {

namespace {

::genesis::RuntimeSceneKind toRuntimeKind(SceneResourceKind kind) {
    return static_cast<::genesis::RuntimeSceneKind>(static_cast<int>(kind));
}

SceneResourceKind fromRuntimeKind(::genesis::RuntimeSceneKind kind) {
    return static_cast<SceneResourceKind>(static_cast<int>(kind));
}

} // namespace

::genesis::RuntimeSceneResource toGenesisRuntimeResource(const ResolvedSceneResource& resource) {
    return {toRuntimeKind(resource.kind), resource.path};
}

std::optional<ResolvedSceneResource> resolveSceneResource(HostContext& host,
                                                          std::string_view resource,
                                                          const std::filesystem::path& module_dir) {
    const auto resolve_storage = [&](std::string_view path) -> std::string {
        const string storage_path = host.resolve_storage_filename(path);
        return std::string(storage_path);
    };

    if (const std::optional<::genesis::RuntimeSceneResource> resolved =
            ::genesis::resolveRuntimeSceneResource(module_dir, resource, resolve_storage)) {
        return ResolvedSceneResource{fromRuntimeKind(resolved->kind), resolved->path};
    }

    host.log_error(std::string("GENESIS scene resource not found: ") + std::string(resource));
    return std::nullopt;
}

} // namespace rxext::genesis_ext
