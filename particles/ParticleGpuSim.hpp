#pragma once

#include "renderer/vulkan/Buffer.hpp"
#include "renderer/vulkan/ComputePipeline.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <volk.h>

namespace genesis {

// GPU particle state (std430). Kept in sync with particle_update.comp.
struct alignas(16) GpuParticle {
    glm::vec4 pos_age{0.0f};     // xyz position, w age
    glm::vec4 vel_life{0.0f};    // xyz velocity, w lifetime
    glm::vec4 size_params{0.0f}; // x size0, y aspect, z stretch
    glm::uvec4 meta{0};          // x flags, y emitter, z emit_index
    glm::vec4 gravity{0.0f};
    glm::vec4 accel_drag{0.0f};  // xyz accel, w drag
    glm::vec4 kill_plane{0.0f};  // x kill_y
};
static_assert(sizeof(GpuParticle) == 112, "GpuParticle must match particle_update.comp");

enum GpuParticleFlags : std::uint32_t {
    kGpuParticleAlive = 1u,
    kGpuParticleGravity = 2u,
    kGpuParticleAccel = 4u,
    kGpuParticleDrag = 8u,
    kGpuParticleKillPlane = 16u,
};

// Host-visible SSBO + compute update for Soft Circles-style motion.
class ParticleGpuSim {
public:
    explicit ParticleGpuSim(const VulkanContext& context);
    ~ParticleGpuSim();

    ParticleGpuSim(const ParticleGpuSim&) = delete;
    ParticleGpuSim& operator=(const ParticleGpuSim&) = delete;

    void ensureCapacity(std::uint32_t count);
    void upload(const GpuParticle* particles, std::uint32_t count);
    void dispatchUpdates(VkCommandBuffer cmd, std::uint32_t count, std::uint32_t steps, float dt,
                         float sim_time_seconds);
    void download(GpuParticle* particles, std::uint32_t count);

    bool ready() const { return pipeline_ != nullptr && buffer_ != nullptr; }
    const VulkanContext& context() const { return context_; }

private:
    struct PushConstants {
        std::uint32_t count = 0;
        float dt = 0.0f;
        float sim_time = 0.0f;
        std::uint32_t pad = 0;
    };

    void createPipeline();
    void writeDescriptors();

    const VulkanContext& context_;
    std::unique_ptr<Buffer> buffer_;
    std::uint32_t capacity_ = 0;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    std::unique_ptr<ComputePipeline> pipeline_;
};

} // namespace genesis
