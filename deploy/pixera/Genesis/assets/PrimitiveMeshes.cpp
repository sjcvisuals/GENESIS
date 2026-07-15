#include "assets/PrimitiveMeshes.hpp"

#include "assets/MeshUpload.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace genesis {

namespace {

constexpr float kPi = 3.14159265358979323846f;

MeshVertexPN vtx(float px, float py, float pz, float nx, float ny, float nz) {
    return MeshVertexPN{px, py, pz, nx, ny, nz};
}

} // namespace

Mesh createCubeMesh(const VulkanContext& context, float half_extent) {
    const float h = half_extent;
    std::vector<MeshVertexPN> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(24);
    indices.reserve(36);

    auto face = [&](float nx, float ny, float nz, MeshVertexPN v0, MeshVertexPN v1, MeshVertexPN v2,
                    MeshVertexPN v3) {
        (void)nx;
        (void)ny;
        (void)nz;
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v3);
        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    };

    // +X
    face(1, 0, 0, vtx(-h, -h, h, 1, 0, 0), vtx(h, -h, h, 1, 0, 0), vtx(h, h, h, 1, 0, 0),
         vtx(-h, h, h, 1, 0, 0));
    // -X
    face(-1, 0, 0, vtx(h, -h, h, -1, 0, 0), vtx(h, -h, -h, -1, 0, 0), vtx(h, h, -h, -1, 0, 0),
         vtx(h, h, h, -1, 0, 0));
    // +Y
    face(0, 1, 0, vtx(-h, h, h, 0, 1, 0), vtx(h, h, h, 0, 1, 0), vtx(h, h, -h, 0, 1, 0),
         vtx(-h, h, -h, 0, 1, 0));
    // -Y
    face(0, -1, 0, vtx(-h, -h, -h, 0, -1, 0), vtx(h, -h, -h, 0, -1, 0), vtx(h, -h, h, 0, -1, 0),
         vtx(-h, -h, h, 0, -1, 0));
    // +Z
    face(0, 0, 1, vtx(-h, -h, h, 0, 0, 1), vtx(h, -h, h, 0, 0, 1), vtx(h, h, h, 0, 0, 1),
         vtx(-h, h, h, 0, 0, 1));
    // -Z
    face(0, 0, -1, vtx(h, -h, -h, 0, 0, -1), vtx(-h, -h, -h, 0, 0, -1), vtx(-h, h, -h, 0, 0, -1),
         vtx(h, h, -h, 0, 0, -1));

    return uploadMesh(context, vertices, indices);
}

Mesh createSphereMesh(const VulkanContext& context, float radius, int segments, int rings) {
    std::vector<MeshVertexPN> vertices;
    std::vector<std::uint32_t> indices;

    segments = std::max(segments, 3);
    rings = std::max(rings, 2);

    for (int y = 0; y <= rings; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(rings);
        const float phi = v * kPi;
        const float sin_phi = std::sin(phi);
        const float cos_phi = std::cos(phi);

        for (int x = 0; x <= segments; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(segments);
            const float theta = u * 2.0f * kPi;
            const float sin_theta = std::sin(theta);
            const float cos_theta = std::cos(theta);

            const float nx = cos_theta * sin_phi;
            const float ny = cos_phi;
            const float nz = sin_theta * sin_phi;

            vertices.push_back(vtx(nx * radius, ny * radius, nz * radius, nx, ny, nz));
        }
    }

    const int stride = segments + 1;
    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            const std::uint32_t i0 = static_cast<std::uint32_t>(y * stride + x);
            const std::uint32_t i1 = i0 + 1;
            const std::uint32_t i2 = i0 + static_cast<std::uint32_t>(stride);
            const std::uint32_t i3 = i2 + 1;

            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);

            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

    return uploadMesh(context, vertices, indices);
}

} // namespace genesis
