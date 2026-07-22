#pragma once

#include "particles/FluidGpuGrid.hpp"
#include "particles/FluidVolume.hpp"
#include "scene/Scene.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

#include <volk.h>

namespace genesis {

struct FluidDrawItem {
    NodeId node_id = 0;
    glm::mat4 model{1.0f};
    const GpuTexture* display_texture = nullptr;
};

class FluidWorld {
public:
    explicit FluidWorld(const VulkanContext& context);

    void clear();
    void sync(Scene& scene);
    void simulate(VkCommandBuffer cmd, Scene& scene, float dt_seconds);

    const std::vector<FluidDrawItem>& drawItems() const { return draw_items_; }

    glm::vec3 sampleFluidVelocity(const Scene& scene, NodeId fluid_node, const glm::vec3& world_pos) const;

private:
    const VulkanContext& context_;
    std::unordered_map<NodeId, std::unique_ptr<FluidGpuGrid>> grids_;
    std::vector<FluidDrawItem> draw_items_;
};

} // namespace genesis
