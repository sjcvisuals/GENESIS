#include "scene/Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace genesis {

namespace {

glm::quat yawPitchQuat(float yaw, float pitch) {
    const glm::quat q_yaw = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat q_pitch = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
    return glm::normalize(q_yaw * q_pitch);
}

} // namespace

void Camera::rebuildRotationFromYawPitch() {
    transform.rotation = yawPitchQuat(yaw_radians, pitch_radians);
}

void Camera::setRotation(const glm::quat& rotation) {
    transform.rotation = glm::normalize(rotation);
}

glm::vec3 Camera::forward() const {
    return transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::vec3 Camera::right() const {
    return transform.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 Camera::up() const {
    return transform.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::mat4 Camera::viewMatrix() const {
    const glm::vec3 eye = transform.position;
    const glm::vec3 center = eye + forward();
    return glm::lookAtRH(eye, center, up());
}

glm::mat4 Camera::projectionMatrix(float aspect) const {
    glm::mat4 projection = glm::perspectiveRH_ZO(fov_y_radians, aspect, near_plane, far_plane);
    // Vulkan NDC has +Y downward; flip clip-space Y to match the framebuffer.
    projection[1][1] *= -1.0f;
    return projection;
}

glm::mat4 Camera::viewProjection(float aspect) const {
    return projectionMatrix(aspect) * viewMatrix();
}

} // namespace genesis
