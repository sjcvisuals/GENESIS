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

Mesh uploadMesh(const VulkanContext& context, std::span<const MeshVertexPN> vertices,
                std::span<const std::uint32_t> indices);

} // namespace genesis
