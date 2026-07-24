#pragma once

#include "renderer/Mesh.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

namespace genesis {

Mesh createCubeMesh(const VulkanContext& context, float half_extent = 0.5f);
Mesh createSphereMesh(const VulkanContext& context, float radius = 0.5f, int segments = 32, int rings = 16);
Mesh createCylinderMesh(const VulkanContext& context, float radius = 0.5f, float height = 1.0f,
                        int segments = 32);
Mesh createPlaneMesh(const VulkanContext& context, float half_extent = 0.5f, float uv_tiles = 1.0f);
// Single quad in local XY (normal +Z) for GPU fluid presentation.
Mesh createFluidDisplayQuadMesh(const VulkanContext& context, float half_extent = 0.5f);

} // namespace genesis
