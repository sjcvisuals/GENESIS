#include "particles/FluidWorld.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <volk.h>

namespace genesis {

FluidWorld::FluidWorld(const VulkanContext& context) : context_(context) {}

void FluidWorld::clear() {
    grids_.clear();
    draw_items_.clear();
}

void FluidWorld::sync(Scene& scene) {
    std::unordered_map<NodeId, std::unique_ptr<FluidGpuGrid>> next;
    for (Node& node : scene.nodes) {
        if (!node.particle_fluid.has_value()) {
            continue;
        }
        if (auto it = grids_.find(node.id); it != grids_.end()) {
            next.emplace(node.id, std::move(it->second));
        } else {
            next.emplace(node.id, std::make_unique<FluidGpuGrid>(context_));
        }
    }
    if (next.size() < grids_.size()) {
        vkDeviceWaitIdle(context_.device());
    }
    grids_ = std::move(next);
}

void FluidWorld::simulate(VkCommandBuffer cmd, Scene& scene, const float dt_seconds) {
    sync(scene);
    draw_items_.clear();

    for (Node& node : scene.nodes) {
        if (!node.visible || !node.particle_fluid.has_value()) {
            continue;
        }
        const FluidVolume& volume = *node.particle_fluid;
        auto it = grids_.find(node.id);
        if (it == grids_.end()) {
            continue;
        }
        it->second->simulate(cmd, volume, dt_seconds);

        FluidDrawItem item{};
        item.node_id = node.id;
        item.display_texture = &it->second->displayTexture();
        item.grid_resolution = it->second->stats().grid_resolution;
        item.atlas_tiles_x = it->second->stats().atlas_tiles_x;
        // Unit box [-0.5,0.5]^3 scaled by extent — sim domain fills this AABB.
        item.model = node.world * glm::scale(glm::mat4(1.0f), volume.extent_meters);
        item.dye_color = volume.dye_color;
        draw_items_.push_back(item);
    }
}

glm::vec3 FluidWorld::sampleFluidVelocity(const Scene& scene, const NodeId fluid_node,
                                          const glm::vec3& world_pos) const {
    const auto grid_it = grids_.find(fluid_node);
    const Node* node = scene.find(fluid_node);
    if (grid_it == grids_.end() || !node || !node->particle_fluid.has_value()) {
        return glm::vec3(0.0f);
    }

    const glm::mat4 inv = glm::inverse(node->world);
    const glm::vec3 local = glm::vec3(inv * glm::vec4(world_pos, 1.0f));
    const glm::vec3 extent = node->particle_fluid->extent_meters;
    if (extent.x <= 0.0f || extent.y <= 0.0f || extent.z <= 0.0f) {
        return glm::vec3(0.0f);
    }

    // Reject outside the full 3D box domain.
    if (std::abs(local.x) > 0.5f * extent.x || std::abs(local.y) > 0.5f * extent.y ||
        std::abs(local.z) > 0.5f * extent.z) {
        return glm::vec3(0.0f);
    }

    const float u = local.x / extent.x + 0.5f;
    const float v = local.y / extent.y + 0.5f;
    const float w = local.z / extent.z + 0.5f;
    const glm::vec3 vel_uvw = grid_it->second->sampleVelocityUvw(u, v, w);
    // Convert UVW/s → local meters/s.
    const glm::vec3 local_vel(vel_uvw.x * extent.x, vel_uvw.y * extent.y, vel_uvw.z * extent.z);
    return glm::mat3(node->world) * local_vel;
}

} // namespace genesis
