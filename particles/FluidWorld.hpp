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
    std::uint32_t grid_resolution = 0;
    std::uint32_t atlas_tiles_x = 0;
    glm::vec4 dye_color{1.0f};
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
    void rebuildDrawItems(Scene& scene);

    const VulkanContext& context_;
    std::unordered_map<NodeId, std::unique_ptr<FluidGpuGrid>> grids_;
    std::vector<FluidDrawItem> draw_items_;
    // Cap simulation to ~30 Hz — lava blobs don't need a full solve every display frame.
    double sim_accumulator_ = 0.0;
    bool has_simulated_once_ = false;
};

} // namespace genesis
