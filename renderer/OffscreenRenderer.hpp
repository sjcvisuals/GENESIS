#pragma once

#include "renderer/FrameUniforms.hpp"
#include "renderer/MeshDrawPushConstants.hpp"

#include <volk.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace genesis {

class VulkanContext;
class ExternalRenderTarget;
class ExternalSync;
class Buffer;
class GraphicsPipeline;
class MaterialBinding;
class GpuTexture;
struct Mesh;

struct OffscreenRendererConfig {
    std::uint64_t adapter_luid = 0;
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    bool enable_validation = false;
};

// Headless renderer for PIXERA RenderBridge: renders into an exportable external target.
class OffscreenRenderer {
public:
    explicit OffscreenRenderer(const OffscreenRendererConfig& config);
    ~OffscreenRenderer();

    OffscreenRenderer(const OffscreenRenderer&) = delete;
    OffscreenRenderer& operator=(const OffscreenRenderer&) = delete;

    void resize(std::uint32_t width, std::uint32_t height);

    void beginFrame();
    void drawMesh(const Mesh& mesh, const MeshDrawPushConstants& draw_constants,
                  const GpuTexture* base_color_texture = nullptr);
    void endFrame(VkSemaphore wait_host_finished = VK_NULL_HANDLE);

    void setFrameUniforms(const FrameUniforms& uniforms);

    const VulkanContext& context() const;
    const ExternalRenderTarget& target() const;
    const ExternalSync& sync() const;
    const char* deviceName() const;

private:
    void createCommandPool();
    void createSyncObjects();
    void createCommandBuffers();
    void createMeshPipeline();
    void recreatePipelines();

    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<ExternalRenderTarget> target_;
    std::unique_ptr<ExternalSync> sync_;
    std::unique_ptr<GraphicsPipeline> mesh_pipeline_;
    std::unique_ptr<MaterialBinding> material_binding_;
    std::unique_ptr<Buffer> frame_ubo_;

    VkDescriptorSetLayout frame_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool frame_descriptor_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> frame_descriptor_sets_;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    static constexpr int kFramesInFlight = 2;
    std::vector<VkFence> in_flight_fences_;

    size_t current_frame_ = 0;
    std::string device_name_;
};

} // namespace genesis
