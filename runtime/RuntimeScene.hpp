#pragma once

#include <filesystem>

namespace genesis {

enum class RuntimeSceneKind {
    Demo,
    Document,
    Gltf,
    Gns,
};

struct RuntimeSceneResource {
    RuntimeSceneKind kind = RuntimeSceneKind::Demo;
    std::filesystem::path path;
};

class SceneRuntime;

bool tryLoadRuntimeScene(SceneRuntime& target, const RuntimeSceneResource& resource,
                         std::string* out_error = nullptr);

} // namespace genesis
