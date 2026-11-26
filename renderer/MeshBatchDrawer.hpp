#pragma once

#include "renderer/BindlessTextures.hpp"
#include "scene/SceneDrawBatch.hpp"

#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <volk.h>

#include <cstdint>
#include <vector>

namespace genesis {

class Buffer;
class ComputePipeline;
class GraphicsPipeline;
class VulkanContext;

struct MeshDrawOptions {
    const float* view_proj_col_major = nullptr;
    bool enable_gpu_cull = false;
    bool enable_depth_prepass = true;
    // When false, prepareDraw must not clear/rebuild bindless (use after another
    // pass already registered textures this frame — e.g. selection outline).
    bool reset_bindless = true;
};

// Optional filter for multi-pipeline draws from one prepareDraw (alpha then additive).
enum class MeshBatchBlendFilter {
    All,
    NonAdditive,
    AdditiveOnly,
};

struct GpuBatchCullHeader {
    std::uint32_t instance_offset = 0;
    std::uint32_t instance_count = 0;
    std::uint32_t visible_offset = 0;
    std::uint32_t visible_count = 0;
};

class MeshBatchDrawer {
public:
    static constexpr std::uint32_t kMaxInstancesPerFrame = 16384;
    static constexpr std::uint32_t kMaxBatchesPerFrame = 512;

    explicit MeshBatchDrawer(const VulkanContext& context);

    ~MeshBatchDrawer();

    void resetFrame();
    void bindInstanceBuffer(VkDevice device, VkDescriptorSet frame_descriptor_set);
    void bindDrawInstanceBuffer(VkDevice device, VkDescriptorSet frame_descriptor_set);

    bool prepareDraw(const std::vector<SceneMeshBatch>& batches, BindlessTextures& bindless,
                     const MeshDrawOptions& options);
    void dispatchGpuCull(VkCommandBuffer cmd);
    void drawDepthPrepass(VkCommandBuffer cmd, const GraphicsPipeline& pipeline,
                          VkDescriptorSet frame_descriptor_set, BindlessTextures& bindless,
                          MeshBatchBlendFilter filter = MeshBatchBlendFilter::All);
    void drawColorPass(VkCommandBuffer cmd, const GraphicsPipeline& pipeline,
                       VkDescriptorSet frame_descriptor_set, BindlessTextures& bindless,
                       VkDescriptorSet display_lut_set = VK_NULL_HANDLE,
                       MeshBatchBlendFilter filter = MeshBatchBlendFilter::All);

    Buffer& instanceBuffer() { return *instance_buffer_; }

private:
    void createCullResources();
    void uploadBatchHeaders();
    void buildIndirectCommands();
    void drawIndexedBatches(VkCommandBuffer cmd, const GraphicsPipeline& pipeline,
                            VkDescriptorSet frame_descriptor_set, BindlessTextures& bindless,
                            VkDescriptorSet display_lut_set, bool depth_only,
                            MeshBatchBlendFilter filter);

    const VulkanContext& context_;

    std::unique_ptr<Buffer> instance_buffer_;
    std::unique_ptr<Buffer> visible_instance_buffer_;
    std::unique_ptr<Buffer> batch_header_buffer_;
    std::unique_ptr<Buffer> indirect_buffer_;

    VkDescriptorSetLayout cull_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout finalize_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool cull_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet cull_descriptor_set_ = VK_NULL_HANDLE;
    VkDescriptorSet finalize_descriptor_set_ = VK_NULL_HANDLE;

    std::unique_ptr<ComputePipeline> cull_pipeline_;
    std::unique_ptr<ComputePipeline> finalize_pipeline_;

    std::vector<SceneMeshBatch> prepared_batches_;
    std::vector<GpuBatchCullHeader> batch_headers_;
    std::array<glm::vec4, 6> frustum_planes_{};
    std::uint32_t total_instances_ = 0;
    std::uint32_t instance_write_index_ = 0;
    bool gpu_cull_enabled_ = false;
    bool frustum_planes_valid_ = false;
    bool prepared_ = false;
};

} // namespace genesis
