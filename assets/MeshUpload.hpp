#pragma once

#include "renderer/Mesh.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

#include <cstdint>
#include <span>

namespace genesis {

struct MeshVertexPN {
    float px, py, pz;
    float nx, ny, nz;
};

struct MeshVertexPNU {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

Mesh uploadMesh(const VulkanContext& context, std::span<const MeshVertexPN> vertices,
                std::span<const std::uint32_t> indices);
Mesh uploadMesh(const VulkanContext& context, std::span<const MeshVertexPNU> vertices,
                std::span<const std::uint32_t> indices);

} // namespace genesis
