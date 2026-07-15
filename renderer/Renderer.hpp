#pragma once

#include <volk.h>
#include <vulkan/vulkan.h>

#include "renderer/FrameUniforms.hpp"

#include "renderer/GridDrawPushConstants.hpp"
#include "renderer/MeshDrawPushConstants.hpp"

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
class MaterialBinding;
class Window;
class GpuTexture;
struct Mesh;

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
    void drawGrid(const Mesh& mesh, const GridDrawPushConstants& draw_constants);
    void endFrame();

    // Temporary API for camera + lighting. Will evolve into render graph inputs later.
    void setFrameUniforms(const FrameUniforms& uniforms);
    void setViewProjection(const float* mat4_col_major);

    void onResize(int width, int height);

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

    Window& window_;
    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<Swapchain> swapchain_;
    std::unique_ptr<GraphicsPipeline> triangle_pipeline_;
    std::unique_ptr<GraphicsPipeline> mesh_pipeline_;
    std::unique_ptr<GraphicsPipeline> grid_pipeline_;
    std::unique_ptr<MaterialBinding> material_binding_;
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
    std::string device_name_;
};

} // namespace genesis
