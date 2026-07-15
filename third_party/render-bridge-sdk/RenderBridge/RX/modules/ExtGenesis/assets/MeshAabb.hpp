#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <span>

namespace genesis {

struct MeshVertexPN;
struct MeshVertexPNU;

inline void computeMeshAabb(std::span<const MeshVertexPN> vertices, glm::vec3& out_min,
                            glm::vec3& out_max) {
    if (vertices.empty()) {
        out_min = glm::vec3(-0.5f);
        out_max = glm::vec3(0.5f);
        return;
    }
    out_min = glm::vec3(vertices[0].px, vertices[0].py, vertices[0].pz);
    out_max = out_min;
    for (const MeshVertexPN& vertex : vertices) {
        const glm::vec3 position(vertex.px, vertex.py, vertex.pz);
        out_min = glm::min(out_min, position);
        out_max = glm::max(out_max, position);
    }
}

inline void computeMeshAabb(std::span<const MeshVertexPNU> vertices, glm::vec3& out_min,
                            glm::vec3& out_max) {
    if (vertices.empty()) {
        out_min = glm::vec3(-0.5f);
        out_max = glm::vec3(0.5f);
        return;
    }
    out_min = glm::vec3(vertices[0].px, vertices[0].py, vertices[0].pz);
    out_max = out_min;
    for (const MeshVertexPNU& vertex : vertices) {
        const glm::vec3 position(vertex.px, vertex.py, vertex.pz);
        out_min = glm::min(out_min, position);
        out_max = glm::max(out_max, position);
    }
}

} // namespace genesis
