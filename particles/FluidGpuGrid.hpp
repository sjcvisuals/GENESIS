#pragma once

#include "particles/FluidVolume.hpp"
#include "renderer/vulkan/Texture.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

#include <memory>
#include <vector>

#include <volk.h>

namespace genesis {

class ComputePipeline;

struct FluidSimStats {
    std::uint32_t grid_resolution = 0;
    std::uint32_t pressure_iterations = 0;
    float last_step_ms = 0.0f;
};

// GPU stable-fluids grid (2D, semi-Lagrangian + Jacobi pressure).
class FluidGpuGrid {
public:
    explicit FluidGpuGrid(const VulkanContext& context);
    ~FluidGpuGrid();

    FluidGpuGrid(const FluidGpuGrid&) = delete;
    FluidGpuGrid& operator=(const FluidGpuGrid&) = delete;

    void ensureResolution(std::uint32_t resolution);
    void simulate(VkCommandBuffer cmd, const FluidVolume& volume, float dt_seconds);

    const GpuTexture& displayTexture() const { return *display_texture_; }
    FluidSimStats stats() const { return stats_; }

    // Bilinear sample of velocity field in volume UV [0,1] (CPU readback cache).
    glm::vec2 sampleVelocityUv(float u, float v) const;

private:
    struct StorageImage {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };

    void destroyImages();
    StorageImage createImage(std::uint32_t size, VkFormat format);
    void destroyImage(StorageImage& image);
    void createPipelines();
    void bindSimDescriptors();

    void dispatchPass(VkCommandBuffer cmd, ComputePipeline& pipeline, std::uint32_t size,
                      const void* push_data, std::uint32_t push_size);
    void swapVelocity();
    void swapDensity();
    void readbackVelocityField();
    void injectSplat(VkCommandBuffer cmd, glm::vec2 uv, glm::vec2 velocity, float radius, float density);

    const VulkanContext& context_;
    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t grid_size_ = 0;
    float sim_time_ = 0.0f;

    StorageImage velocity_[2]{};
    StorageImage density_[2]{};
    StorageImage pressure_[2]{};
    StorageImage divergence_{};
    int velocity_index_ = 0;
    int density_index_ = 0;
    int pressure_index_ = 0;

    std::unique_ptr<GpuTexture> display_texture_;
    VkImageLayout display_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;

    std::unique_ptr<ComputePipeline> advect_pipeline_;
    std::unique_ptr<ComputePipeline> divergence_pipeline_;
    std::unique_ptr<ComputePipeline> jacobi_pipeline_;
    std::unique_ptr<ComputePipeline> gradient_pipeline_;
    std::unique_ptr<ComputePipeline> splat_pipeline_;
    std::unique_ptr<ComputePipeline> present_pipeline_;

    std::vector<float> velocity_readback_;
    FluidSimStats stats_{};
};

} // namespace genesis
