#pragma once

#include "scene/SceneCamera.hpp"
#include "scene/SceneLight.hpp"
#include "scene/SceneMaterial.hpp"
#include "scene/Transform.hpp"

#include <optional>
#include <string>
#include <vector>

namespace genesis {

struct SceneDocumentNode {
    std::string path;
    std::string name;
    std::optional<std::string> parent_path;
    Transform local{};
    bool visible = true;
    bool pickable = true;

    std::optional<std::string> mesh_ref;
    std::optional<std::string> material_ref;

    std::optional<SceneCamera> camera;
    std::optional<SceneLight> light;
};

struct SceneDocument {
    int version = 1;
    std::string source; // glTF path relative to assets, or "builtin"
    std::optional<std::string> active_output_camera;

    std::vector<std::pair<std::string, SceneMaterial>> materials;
    std::vector<SceneDocumentNode> nodes;
};

} // namespace genesis
