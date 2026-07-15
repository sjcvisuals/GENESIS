#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>

#include <array>
#include <cmath>

namespace genesis {

struct FrustumPlane {
    glm::vec3 normal{};
    float distance = 0.0f;

    float signedDistance(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }
};

struct Frustum {
    std::array<FrustumPlane, 6> planes{};
};

inline Frustum frustumFromViewProjection(const glm::mat4& view_projection) {
    Frustum frustum{};

    const glm::vec4 row0 = glm::row(view_projection, 0);
    const glm::vec4 row1 = glm::row(view_projection, 1);
    const glm::vec4 row2 = glm::row(view_projection, 2);
    const glm::vec4 row3 = glm::row(view_projection, 3);

    const std::array<glm::vec4, 6> combos = {
        row3 + row0, // left
        row3 - row0, // right
        row3 + row1, // bottom
        row3 - row1, // top
        row3 + row2, // near
        row3 - row2, // far
    };

    for (std::size_t i = 0; i < combos.size(); ++i) {
        const glm::vec3 normal = glm::vec3(combos[i]);
        const float length = glm::length(normal);
        if (length > 1e-6f) {
            frustum.planes[i].normal = normal / length;
            frustum.planes[i].distance = combos[i].w / length;
        }
    }

    return frustum;
}

inline bool frustumContainsPoint(const Frustum& frustum, const glm::vec3& point) {
    for (const FrustumPlane& plane : frustum.planes) {
        if (plane.signedDistance(point) < 0.0f) {
            return false;
        }
    }
    return true;
}

inline bool frustumIntersectsAabb(const Frustum& frustum, const glm::vec3& min_corner,
                                  const glm::vec3& max_corner) {
    for (const FrustumPlane& plane : frustum.planes) {
        const glm::vec3 positive{
            plane.normal.x >= 0.0f ? max_corner.x : min_corner.x,
            plane.normal.y >= 0.0f ? max_corner.y : min_corner.y,
            plane.normal.z >= 0.0f ? max_corner.z : min_corner.z,
        };
        if (plane.signedDistance(positive) < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace genesis
