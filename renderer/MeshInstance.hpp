#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <array>
#include <cstring>

namespace genesis {

struct GpuMeshInstance {
    alignas(16) glm::mat4 model{1.0f};
    alignas(16) glm::mat4 normal_matrix{1.0f};
    alignas(16) glm::vec4 bounds_center{0.0f}; // xyz = world center, w unused
    alignas(16) glm::vec4 bounds_extent{0.5f}; // xyz = world half extents, w unused
};

static_assert(sizeof(GpuMeshInstance) == 160, "GpuMeshInstance size mismatch");

inline void fillGpuMeshInstance(GpuMeshInstance& out, const glm::mat4& model,
                                const glm::vec3& local_aabb_min, const glm::vec3& local_aabb_max) {
    out.model = model;
    const glm::mat3 normal = glm::transpose(glm::inverse(glm::mat3(model)));
    out.normal_matrix = glm::mat4(normal);

    const std::array<glm::vec3, 8> corners = {
        glm::vec3(local_aabb_min.x, local_aabb_min.y, local_aabb_min.z),
        glm::vec3(local_aabb_max.x, local_aabb_min.y, local_aabb_min.z),
        glm::vec3(local_aabb_min.x, local_aabb_max.y, local_aabb_min.z),
        glm::vec3(local_aabb_max.x, local_aabb_max.y, local_aabb_min.z),
        glm::vec3(local_aabb_min.x, local_aabb_min.y, local_aabb_max.z),
        glm::vec3(local_aabb_max.x, local_aabb_min.y, local_aabb_max.z),
        glm::vec3(local_aabb_min.x, local_aabb_max.y, local_aabb_max.z),
        glm::vec3(local_aabb_max.x, local_aabb_max.y, local_aabb_max.z),
    };

    glm::vec3 min_corner = glm::vec3(model * glm::vec4(corners[0], 1.0f));
    glm::vec3 max_corner = min_corner;
    for (std::size_t i = 1; i < corners.size(); ++i) {
        const glm::vec3 transformed = glm::vec3(model * glm::vec4(corners[i], 1.0f));
        min_corner = glm::min(min_corner, transformed);
        max_corner = glm::max(max_corner, transformed);
    }

    out.bounds_center = glm::vec4((min_corner + max_corner) * 0.5f, 0.0f);
    out.bounds_extent = glm::vec4((max_corner - min_corner) * 0.5f, 0.0f);
}

} // namespace genesis
