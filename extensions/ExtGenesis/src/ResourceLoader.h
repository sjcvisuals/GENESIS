#pragma once

#include "rxext_client.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace rxext::genesis_ext {

enum class SceneResourceKind {
    Demo,
    Document,
    Gltf,
    Gns,
};

struct ResolvedSceneResource {
    SceneResourceKind kind = SceneResourceKind::Demo;
    std::filesystem::path path;
};

// Resolve a PIXERA/RX scene_resource string to a loadable resource.
std::optional<ResolvedSceneResource> resolveSceneResource(HostContext& host,
                                                          std::string_view resource,
                                                          const std::filesystem::path& module_dir);

} // namespace rxext::genesis_ext
