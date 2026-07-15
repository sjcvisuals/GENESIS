#include "scene/SceneCamera.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace genesis {

namespace {

glm::vec3 worldPosition(const glm::mat4& world) {
    return glm::vec3(world[3]);
}

glm::vec3 worldDirection(const glm::mat4& world, const glm::vec3& local_axis) {
    return glm::normalize(glm::vec3(world * glm::vec4(local_axis, 0.0f)));
}

} // namespace

glm::mat4 SceneCamera::viewMatrix(const glm::mat4& node_world) const {
    const glm::vec3 eye = worldPosition(node_world);
    const glm::vec3 forward = worldDirection(node_world, glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::vec3 up = worldDirection(node_world, glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::lookAtRH(eye, eye + forward, up);
}

glm::mat4 SceneCamera::projectionMatrix(float aspect) const {
    glm::mat4 projection = glm::perspectiveRH_ZO(fov_y_radians, aspect, near_plane, far_plane);
    projection[1][1] *= -1.0f;
    return projection;
}

glm::mat4 SceneCamera::viewProjection(const glm::mat4& node_world, float aspect) const {
    return projectionMatrix(aspect) * viewMatrix(node_world);
}

} // namespace genesis
