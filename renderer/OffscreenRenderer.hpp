#pragma once

#include "renderer/FrameUniforms.hpp"
#include "renderer/MeshBatchDrawer.hpp"
#include "renderer/MeshDrawPushConstants.hpp"
#include "scene/SceneDrawBatch.hpp"

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
class BindlessTextures;
class DisplayLutBinding;
class MeshBatchDrawer;
class GpuTexture;
class SceneRuntime;
struct Mesh;
struct ColorLut3D;

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
    // Additional output on an existing Vulkan device (shared meshes/textures/scene).
    OffscreenRenderer(VulkanContext& shared_context, std::uint32_t width, std::uint32_t height);
    ~OffscreenRenderer();

    OffscreenRenderer(const OffscreenRenderer&) = delete;
    OffscreenRenderer& operator=(const OffscreenRenderer&) = delete;

    void resize(std::uint32_t width, std::uint32_t height);

    void beginFrame();
    void simulateFluids(SceneRuntime& runtime, float dt_seconds);
    void drawMesh(const Mesh& mesh, const MeshDrawPushConstants& draw_constants,
                  const GpuTexture* base_color_texture = nullptr);
    void drawMeshBatches(const std::vector<SceneMeshBatch>& batches,
                         const MeshDrawOptions& options = {});
    void endFrame(VkSemaphore wait_host_finished = VK_NULL_HANDLE);
    void waitFrameComplete();

    void setFrameUniforms(const FrameUniforms& uniforms);

    // Default is transparent (RGB black, A=0) so PIXERA can composite over clips behind.
    void setClearColor(float r, float g, float b, float a);
    void uploadDisplayLut(const ColorLut3D& lut);

    const VulkanContext& context() const;
    const ExternalRenderTarget& target() const;
    const ExternalSync& sync() const;
    const char* deviceName() const;

private:
    void createCommandPool();
    void createSyncObjects();
    void createCommandBuffers();
    void ensureRenderPass();
    void createMeshPipeline();
    void createMeshPipelines();
    void recreatePipelines();

    std::unique_ptr<VulkanContext> owned_context_;
    VulkanContext* context_ = nullptr;
    bool owns_context_ = true;
    std::unique_ptr<ExternalRenderTarget> target_;
    std::unique_ptr<ExternalSync> sync_;
    std::unique_ptr<GraphicsPipeline> mesh_pipeline_;
    std::unique_ptr<GraphicsPipeline> mesh_depth_pipeline_;
    std::unique_ptr<BindlessTextures> bindless_textures_;
    std::unique_ptr<DisplayLutBinding> display_lut_;
    std::unique_ptr<MeshBatchDrawer> mesh_batch_drawer_;
    std::unique_ptr<Buffer> frame_ubo_;

    VkDescriptorSetLayout frame_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool frame_descriptor_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> frame_descriptor_sets_;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    static constexpr int kFramesInFlight = 2;
    std::vector<VkFence> in_flight_fences_;

    size_t current_frame_ = 0;
    bool render_pass_active_ = false;
    FrameUniforms frame_uniforms_{};
    // Default is transparent (RGB black, A=0) so PIXERA can composite over clips behind.
    // Windowed Renderer uses an opaque studio backdrop instead.
    float clear_color_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    std::string device_name_;
};

} // namespace genesis
