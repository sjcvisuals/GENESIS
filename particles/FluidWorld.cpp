#include "particles/FluidWorld.hpp"

#include "scene/SceneVisibility.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <volk.h>

namespace genesis {

namespace {
constexpr double kFluidSimHz = 30.0;
constexpr double kFluidSimStep = 1.0 / kFluidSimHz;
constexpr float kFluidMaxStepSeconds = 1.0f / 20.0f; // avoid huge catch-up steps
} // namespace

FluidWorld::FluidWorld(const VulkanContext& context) : context_(context) {}

void FluidWorld::clear() {
    grids_.clear();
    draw_items_.clear();
    sim_accumulator_ = 0.0;
    has_simulated_once_ = false;
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

void FluidWorld::rebuildDrawItems(Scene& scene) {
    draw_items_.clear();
    for (Node& node : scene.nodes) {
        // Editor-time gate only (Visible + ancestry). Hidden-in-Game is applied at append
        // with game_output so Game View / Renderbridge stay in lockstep with meshes.
        if (!node.particle_fluid.has_value() || !isNodeDrawnInView(scene, node, false)) {
            continue;
        }
        const FluidVolume& volume = *node.particle_fluid;
        auto it = grids_.find(node.id);
        if (it == grids_.end() || !volume.enabled) {
            continue;
        }
        // Ensure display target exists even on skipped sim frames.
        it->second->ensureResolution(volume.grid_resolution);
        FluidDrawItem item{};
        item.node_id = node.id;
        item.display_texture = &it->second->displayTexture();
        item.grid_resolution = it->second->stats().grid_resolution;
        item.atlas_tiles_x = it->second->stats().atlas_tiles_x;
        item.model = node.world * glm::scale(glm::mat4(1.0f), volume.extent_meters);
        item.dye_color = volume.dye_color;
        draw_items_.push_back(item);
    }
}

void FluidWorld::simulate(VkCommandBuffer cmd, Scene& scene, const float dt_seconds) {
    sync(scene);

    sim_accumulator_ += static_cast<double>(std::max(dt_seconds, 0.0f));
    const bool need_bootstrap = !has_simulated_once_;
    const bool due = sim_accumulator_ >= kFluidSimStep || need_bootstrap;
    if (due) {
        float step = static_cast<float>(std::min(sim_accumulator_, static_cast<double>(kFluidMaxStepSeconds)));
        if (need_bootstrap) {
            step = std::max(step, static_cast<float>(kFluidSimStep));
        }
        sim_accumulator_ = std::max(0.0, sim_accumulator_ - static_cast<double>(step));
        // Keep at most one pending step so hitch recovery doesn't stack solves.
        if (sim_accumulator_ > kFluidSimStep) {
            sim_accumulator_ = kFluidSimStep;
        }

        for (Node& node : scene.nodes) {
            if (!node.particle_fluid.has_value() || !isNodeDrawnInView(scene, node, false)) {
                continue;
            }
            auto it = grids_.find(node.id);
            if (it == grids_.end()) {
                continue;
            }
            it->second->simulate(cmd, *node.particle_fluid, step);
        }
        has_simulated_once_ = true;
    }

    rebuildDrawItems(scene);
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
