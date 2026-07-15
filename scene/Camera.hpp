#pragma once

#include "scene/Transform.hpp"

#include <glm/glm.hpp>

namespace genesis {

class Camera {
public:
    Transform transform{};

    float fov_y_radians = glm::radians(60.0f);
    float near_plane = 0.1f;
    float far_plane = 200.0f;

    // FPS-style control parameters (rebuild transform.rotation via rebuildRotationFromYawPitch).
    float yaw_radians = 0.0f;   // around +Y
    float pitch_radians = 0.0f; // around +X

    void rebuildRotationFromYawPitch();
    void setRotation(const glm::quat& rotation);

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspect) const;
    glm::mat4 viewProjection(float aspect) const;

    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 up() const;
};

} // namespace genesis
