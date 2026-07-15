#pragma once

#include "renderer/Mesh.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

namespace genesis {

Mesh createCubeMesh(const VulkanContext& context, float half_extent = 0.5f);
Mesh createSphereMesh(const VulkanContext& context, float radius = 0.5f, int segments = 32, int rings = 16);

} // namespace genesis
