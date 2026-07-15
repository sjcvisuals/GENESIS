#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace genesis {

enum class SceneLightType : int {
    Directional = 0,
    Point = 1,
    Spot = 2,
};

struct SceneLight {
    SceneLightType type = SceneLightType::Directional;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    bool enabled = true;

    // Point / spot
    float range = 10.0f;

    // Spot only (radians)
    float inner_cone_radians = glm::pi<float>() / 9.0f;  // 20 deg
    float outer_cone_radians = glm::pi<float>() / 6.0f;  // 30 deg
};

} // namespace genesis
