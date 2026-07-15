#pragma once

#include <glm/gtc/constants.hpp>
#include <glm/mat4x4.hpp>

namespace genesis {

struct SceneCamera {
    float fov_y_radians = glm::pi<float>() / 3.0f; // 60 degrees
    float near_plane = 0.1f;
    float far_plane = 200.0f;

    glm::mat4 viewMatrix(const glm::mat4& node_world) const;
    glm::mat4 projectionMatrix(float aspect) const;
    glm::mat4 viewProjection(const glm::mat4& node_world, float aspect) const;
};

} // namespace genesis
