#pragma once

#include "scene/Frustum.hpp"
#include "renderer/Mesh.hpp"

#include <glm/glm.hpp>

#include <array>

namespace genesis {

inline glm::vec3 worldScaleFromMatrix(const glm::mat4& world) {
    return glm::vec3(glm::length(glm::vec3(world[0])),
                     glm::length(glm::vec3(world[1])),
                     glm::length(glm::vec3(world[2])));
}

inline void worldAabbCorners(const glm::mat4& world, const glm::vec3& local_min,
                             const glm::vec3& local_max, glm::vec3& out_min, glm::vec3& out_max) {
    const std::array<glm::vec3, 8> corners = {
        glm::vec3(local_min.x, local_min.y, local_min.z),
        glm::vec3(local_max.x, local_min.y, local_min.z),
        glm::vec3(local_min.x, local_max.y, local_min.z),
        glm::vec3(local_max.x, local_max.y, local_min.z),
        glm::vec3(local_min.x, local_min.y, local_max.z),
        glm::vec3(local_max.x, local_min.y, local_max.z),
        glm::vec3(local_min.x, local_max.y, local_max.z),
        glm::vec3(local_max.x, local_max.y, local_max.z),
    };

    out_min = glm::vec3(world * glm::vec4(corners[0], 1.0f));
    out_max = out_min;
    for (std::size_t i = 1; i < corners.size(); ++i) {
        const glm::vec3 transformed = glm::vec3(world * glm::vec4(corners[i], 1.0f));
        out_min = glm::min(out_min, transformed);
        out_max = glm::max(out_max, transformed);
    }
}

inline bool isMeshVisibleInFrustum(const Frustum& frustum, const glm::mat4& world,
                                   const glm::vec3& local_aabb_min, const glm::vec3& local_aabb_max) {
    glm::vec3 min_corner{};
    glm::vec3 max_corner{};
    worldAabbCorners(world, local_aabb_min, local_aabb_max, min_corner, max_corner);
    return frustumIntersectsAabb(frustum, min_corner, max_corner);
}

inline void worldBoundsFromMesh(const glm::mat4& world, const glm::vec3& local_aabb_min,
                                const glm::vec3& local_aabb_max, glm::vec3& out_center,
                                glm::vec3& out_half_extent) {
    glm::vec3 min_corner{};
    glm::vec3 max_corner{};
    worldAabbCorners(world, local_aabb_min, local_aabb_max, min_corner, max_corner);
    out_center = (min_corner + max_corner) * 0.5f;
    out_half_extent = (max_corner - min_corner) * 0.5f;
}

} // namespace genesis
