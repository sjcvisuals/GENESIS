#include "assets/PrimitiveMeshes.hpp"

#include "assets/MeshUpload.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace genesis {

namespace {

constexpr float kPi = 3.14159265358979323846f;

MeshVertexPNU vtx(float px, float py, float pz, float nx = 0.0f, float ny = 0.0f, float nz = 1.0f,
                  float u = 0.0f, float v = 0.0f) {
    return MeshVertexPNU{px, py, pz, nx, ny, nz, u, v};
}

} // namespace

Mesh createCubeMesh(const VulkanContext& context, float half_extent) {
    const float h = half_extent;
    std::vector<MeshVertexPNU> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(24);
    indices.reserve(36);

    // Outward CCW winding (Vulkan front face = counter-clockwise).
    // Each face gets a full 0-1 UV square so albedo / MR / normal maps read correctly.
    auto face = [&](float nx, float ny, float nz, MeshVertexPNU v0, MeshVertexPNU v1, MeshVertexPNU v2,
                    MeshVertexPNU v3) {
        v0.nx = nx;
        v0.ny = ny;
        v0.nz = nz;
        v0.u = 0.0f;
        v0.v = 1.0f;
        v1.nx = nx;
        v1.ny = ny;
        v1.nz = nz;
        v1.u = 1.0f;
        v1.v = 1.0f;
        v2.nx = nx;
        v2.ny = ny;
        v2.nz = nz;
        v2.u = 1.0f;
        v2.v = 0.0f;
        v3.nx = nx;
        v3.ny = ny;
        v3.nz = nz;
        v3.u = 0.0f;
        v3.v = 0.0f;
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
    face(1, 0, 0, vtx(h, -h, h), vtx(h, -h, -h), vtx(h, h, -h), vtx(h, h, h));
    // -X
    face(-1, 0, 0, vtx(-h, -h, -h), vtx(-h, -h, h), vtx(-h, h, h), vtx(-h, h, -h));
    // +Y
    face(0, 1, 0, vtx(-h, h, h), vtx(h, h, h), vtx(h, h, -h), vtx(-h, h, -h));
    // -Y
    face(0, -1, 0, vtx(-h, -h, -h), vtx(h, -h, -h), vtx(h, -h, h), vtx(-h, -h, h));
    // +Z
    face(0, 0, 1, vtx(-h, -h, h), vtx(h, -h, h), vtx(h, h, h), vtx(-h, h, h));
    // -Z
    face(0, 0, -1, vtx(h, -h, -h), vtx(-h, -h, -h), vtx(-h, h, -h), vtx(h, h, -h));

    return uploadMesh(context, vertices, indices);
}

Mesh createSphereMesh(const VulkanContext& context, float radius, int segments, int rings) {
    std::vector<MeshVertexPNU> vertices;
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

            vertices.push_back(vtx(nx * radius, ny * radius, nz * radius, nx, ny, nz, u, v));
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

Mesh createCylinderMesh(const VulkanContext& context, float radius, float height, int segments) {
    std::vector<MeshVertexPNU> vertices;
    std::vector<std::uint32_t> indices;

    segments = std::max(segments, 3);
    const float half_height = height * 0.5f;

    // Side wall: seamless U wrap, V from bottom (0) to top (1).
    for (int seg = 0; seg <= segments; ++seg) {
        const float u = static_cast<float>(seg) / static_cast<float>(segments);
        const float theta = u * 2.0f * kPi;
        const float nx = std::cos(theta);
        const float nz = std::sin(theta);
        const float px = nx * radius;
        const float pz = nz * radius;

        vertices.push_back(vtx(px, -half_height, pz, nx, 0.0f, nz, u, 1.0f));
        vertices.push_back(vtx(px, half_height, pz, nx, 0.0f, nz, u, 0.0f));
    }

    const std::uint32_t side_count = static_cast<std::uint32_t>((segments + 1) * 2);

    // Caps use dedicated verts so normals and radial UVs are correct.
    const std::uint32_t top_center = side_count;
    vertices.push_back(vtx(0.0f, half_height, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f));
    const std::uint32_t bottom_center = top_center + 1;
    vertices.push_back(vtx(0.0f, -half_height, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 0.5f));

    const std::uint32_t top_rim0 = bottom_center + 1;
    for (int seg = 0; seg <= segments; ++seg) {
        const float t = static_cast<float>(seg) / static_cast<float>(segments);
        const float theta = t * 2.0f * kPi;
        const float nx = std::cos(theta);
        const float nz = std::sin(theta);
        const float px = nx * radius;
        const float pz = nz * radius;
        const float u = nx * 0.5f + 0.5f;
        const float v = nz * 0.5f + 0.5f;
        vertices.push_back(vtx(px, half_height, pz, 0.0f, 1.0f, 0.0f, u, v));
    }

    const std::uint32_t bottom_rim0 = top_rim0 + static_cast<std::uint32_t>(segments + 1);
    for (int seg = 0; seg <= segments; ++seg) {
        const float t = static_cast<float>(seg) / static_cast<float>(segments);
        const float theta = t * 2.0f * kPi;
        const float nx = std::cos(theta);
        const float nz = std::sin(theta);
        const float px = nx * radius;
        const float pz = nz * radius;
        const float u = nx * 0.5f + 0.5f;
        const float v = 1.0f - (nz * 0.5f + 0.5f);
        vertices.push_back(vtx(px, -half_height, pz, 0.0f, -1.0f, 0.0f, u, v));
    }

    for (int seg = 0; seg < segments; ++seg) {
        const std::uint32_t bottom0 = static_cast<std::uint32_t>(seg * 2);
        const std::uint32_t top0 = bottom0 + 1;
        const std::uint32_t bottom1 = bottom0 + 2;
        const std::uint32_t top1 = bottom0 + 3;

        indices.push_back(bottom0);
        indices.push_back(top0);
        indices.push_back(bottom1);
        indices.push_back(bottom1);
        indices.push_back(top0);
        indices.push_back(top1);

        const std::uint32_t top_a = top_rim0 + static_cast<std::uint32_t>(seg);
        const std::uint32_t top_b = top_a + 1;
        indices.push_back(top_center);
        indices.push_back(top_a);
        indices.push_back(top_b);

        const std::uint32_t bottom_a = bottom_rim0 + static_cast<std::uint32_t>(seg);
        const std::uint32_t bottom_b = bottom_a + 1;
        indices.push_back(bottom_center);
        indices.push_back(bottom_b);
        indices.push_back(bottom_a);
    }

    return uploadMesh(context, vertices, indices);
}

Mesh createPlaneMesh(const VulkanContext& context, float half_extent) {
    const float h = half_extent;
    // +Y facing, full 0-1 UV square (matches cube +Y face orientation).
    std::vector<MeshVertexPNU> vertices = {
        vtx(-h, 0.0f, h, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f),
        vtx(h, 0.0f, h, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f),
        vtx(h, 0.0f, -h, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f),
        vtx(-h, 0.0f, -h, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
    };
    std::vector<std::uint32_t> indices = {0, 1, 2, 0, 2, 3};
    return uploadMesh(context, vertices, indices);
}

Mesh createFluidDisplayQuadMesh(const VulkanContext& context, const float half_extent) {
    const float h = half_extent;
    // Same winding as the +Z face of createCubeMesh — matches fluid sim UV space (XY).
    std::vector<MeshVertexPNU> vertices = {
        vtx(-h, -h, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
        vtx(h, -h, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f),
        vtx(h, h, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f),
        vtx(-h, h, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f),
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2, 0, 2, 3};
    return uploadMesh(context, vertices, indices);
}

} // namespace genesis
