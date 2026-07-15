#pragma once

#include "renderer/Mesh.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

namespace genesis {

// XZ grid centered at origin. half_extent and divisions define cell size.
// With 1 world unit = 1 m, cell_size_m * 100 = cell size in cm.
Mesh createEditorGridMesh(const VulkanContext& context, float half_extent_m = 5.0f, int divisions = 10);

} // namespace genesis
