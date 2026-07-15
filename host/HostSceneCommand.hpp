#pragma once

#include "scene/SceneLight.hpp"
#include "scene/Transform.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace genesis {

// Stable, path-keyed commands a host can send over a link (RenderBridge, IPC, etc.).
enum class HostSceneCommandType : std::uint8_t {
    LoadDemo,
    LoadGltf,
    LoadDocument,
    SaveDocument,
    SetOutputCamera,
    ClearOutputCamera,
    SetNodeTransform,
    SetNodeVisible,
    SetMaterialBaseColor,
    SetMaterialMetallicRoughness,
    SetLightEnabled,
    SetLightIntensity,
};

struct HostSceneCommand {
    HostSceneCommandType type = HostSceneCommandType::LoadDemo;

    // Primary path key (node path, camera path, or file path depending on command).
    std::string path;

    std::optional<Transform> transform;
    std::optional<bool> visible;
    std::optional<glm::vec4> base_color;
    std::optional<float> metallic;
    std::optional<float> roughness;
    std::optional<bool> light_enabled;
    std::optional<float> light_intensity;
};

} // namespace genesis
