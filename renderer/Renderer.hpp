#pragma once

#include <volk.h>
#include <vulkan/vulkan.h>

#include "renderer/FrameUniforms.hpp"

#include "renderer/GridDrawPushConstants.hpp"
#include "renderer/MeshBatchDrawer.hpp"
#include "renderer/MeshDrawPushConstants.hpp"
#include "scene/SceneDrawBatch.hpp"

#include "renderer/DisplayLutBinding.hpp"
#include "color/ColorManagement.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;

namespace genesis {

class VulkanContext;
class Swapchain;
class Buffer;
class GraphicsPipeline;
class ImGuiLayer;
class BindlessTextures;
class DisplayLutBinding;
class MeshBatchDrawer;
class Window;
class GpuTexture;
struct Mesh;
struct ColorLut3D;

class Renderer {
public:
    explicit Renderer(Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void beginUiFrame();
    void beginFrame();
    void drawTriangle();
    void drawMesh(const Mesh& mesh, const MeshDrawPushConstants& draw_constants,
                  const GpuTexture* base_color_texture = nullptr);
    void drawMeshBatches(const std::vector<SceneMeshBatch>& batches,
                         const MeshDrawOptions& options = {});
    void simulateFluids(class SceneRuntime& runtime, float dt_seconds);
    void drawGrid(const Mesh& mesh, const GridDrawPushConstants& draw_constants);
    void endFrame();

    // Temporary API for camera + lighting. Will evolve into render graph inputs later.
    void setFrameUniforms(const FrameUniforms& uniforms);
    void setViewProjection(const float* mat4_col_major);

    // Default is transparent (RGB black, A=0) so PIXERA can composite over clips behind.
    // Call with A=1 when an explicit solid background colour is desired.
    void setClearColor(float r, float g, float b, float a);
    void uploadDisplayLut(const ColorLut3D& lut);
    DisplayLutBinding* displayLut() { return display_lut_.get(); }

    // When enabled, mesh/grid draws are clipped to this screen-space rectangle (pixels).
    void setViewportDrawRegion(float x, float y, float width, float height);
    void clearViewportDrawRegion();

    void onResize(int width, int height);
    void waitIdle();

    void enableImGuiLayoutPersistence();

    VkInstance instance() const;
    const VulkanContext& context() const;
    const char* deviceName() const;

private:
    void createCommandPool();
    void createSyncObjects();
    void createCommandBuffers();
    void createTriangleResources();
    void createMeshResources();
    void createGridResources();
    void createFrameDescriptorSetLayout();
    void createFrameDescriptorPool();
    void createFrameDescriptorSets();
    void recreateSwapchain(int width, int height);

    void ensureRenderPass();
    void createMeshPipelines();
    void applyDrawRegion(VkCommandBuffer cmd) const;

    Window& window_;
    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<Swapchain> swapchain_;
    std::unique_ptr<GraphicsPipeline> triangle_pipeline_;
    std::unique_ptr<GraphicsPipeline> mesh_pipeline_;
    std::unique_ptr<GraphicsPipeline> mesh_depth_pipeline_;
    std::unique_ptr<GraphicsPipeline> grid_pipeline_;
    std::unique_ptr<BindlessTextures> bindless_textures_;
    std::unique_ptr<DisplayLutBinding> display_lut_;
    std::unique_ptr<MeshBatchDrawer> mesh_batch_drawer_;
    std::unique_ptr<Buffer> vertex_buffer_;
    std::unique_ptr<ImGuiLayer> imgui_;

    std::unique_ptr<Buffer> frame_ubo_;
    VkDescriptorSetLayout frame_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool frame_descriptor_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> frame_descriptor_sets_;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    static constexpr int kFramesInFlight = 2;
    std::vector<VkSemaphore> image_available_semaphores_;
    std::vector<VkSemaphore> render_finished_semaphores_;
    std::vector<VkFence> in_flight_fences_;

    size_t current_frame_ = 0;
    uint32_t image_index_ = 0;
    bool framebuffer_resized_ = false;
    bool render_pass_active_ = false;
    FrameUniforms frame_uniforms_{};
    // Soft studio backdrop — reads better than pure black with PBR materials.
    float clear_color_[4] = {0.035f, 0.038f, 0.045f, 1.0f};
    std::string device_name_;

    bool draw_region_enabled_ = false;
    float draw_region_x_ = 0.0f;
    float draw_region_y_ = 0.0f;
    float draw_region_w_ = 0.0f;
    float draw_region_h_ = 0.0f;
};

} // namespace genesis
