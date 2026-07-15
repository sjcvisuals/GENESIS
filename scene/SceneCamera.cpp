#include "scene/SceneCamera.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace genesis {

glm::mat4 SceneCamera::viewMatrix(const glm::mat4& node_world) const {
    return glm::inverse(node_world);
}

glm::mat4 SceneCamera::projectionMatrix(float aspect) const {
    return glm::perspectiveRH_ZO(fov_y_radians, aspect, near_plane, far_plane);
}

glm::mat4 SceneCamera::viewProjection(const glm::mat4& node_world, float aspect) const {
    return projectionMatrix(aspect) * viewMatrix(node_world);
}

} // namespace genesis
