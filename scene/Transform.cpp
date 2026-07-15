#include "scene/Transform.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace genesis {

glm::mat4 Transform::matrix() const {
    const glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
    const glm::mat4 r = glm::mat4_cast(rotation);
    const glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
    return t * r * s;
}

} // namespace genesis
