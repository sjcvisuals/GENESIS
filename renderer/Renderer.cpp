#include "renderer/Renderer.hpp"

#include "renderer/FrameUniforms.hpp"
#include "renderer/GridDrawPushConstants.hpp"
#include "renderer/MeshInstance.hpp"

#include "platform/ImGuiLayer.hpp"
#include "platform/Paths.hpp"
#include "platform/Window.hpp"
#include "renderer/vulkan/Buffer.hpp"
#include "renderer/vulkan/Pipeline.hpp"
#include "renderer/vulkan/Swapchain.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/MeshBatchDrawer.hpp"
#include "renderer/BindlessTextures.hpp"
#include "renderer/DisplayLutBinding.hpp"
#include "color/ColorManagement.hpp"
#include "renderer/Mesh.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <array>
#include <filesystem>
#include <string>
#include <glm/glm.hpp>
#include <cstring>
#include <cstring>

namespace genesis {

namespace {

struct Vertex {
    float position[3];
    float color[3];
};

const std::array<Vertex, 3> kTriangleVertices = {{
    // CCW when viewed from +Z so Vulkan back-face culling keeps the face.
    {{0.0f, -0.5f, 0.0f}, {1.0f, 0.2f, 0.3f}},
    {{-0.5f, 0.5f, 0.0f}, {0.3f, 0.4f, 1.0f}},
    {{0.5f, 0.5f, 0.0f}, {0.2f, 1.0f, 0.4f}},
}};

} // namespace

Renderer::Renderer(Window& window) : window_(window) {
#ifndef NDEBUG
    const bool enable_validation = true;
#else
    const bool enable_validation = false;
#endif

    VulkanContextConfig context_config{};
    context_config.window = window.handle();
    context_config.enable_validation = enable_validation;

    context_ = std::make_unique<VulkanContext>(context_config);

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(context_->physicalDevice(), &props);
    device_name_ = props.deviceName;

    SwapchainConfig swapchain_config{};
    swapchain_config.width = window.width();
    swapchain_config.height = window.height();
    spdlog::debug("Creating swapchain...");
    swapchain_ = std::make_unique<Swapchain>(*context_, swapchain_config);
    spdlog::debug("Swapchain created ({}x{})", swapchain_->extent().width, swapchain_->extent().height);

    createCommandPool();
    createSyncObjects();
    createCommandBuffers();

    spdlog::debug("Creating triangle pipeline...");
    createTriangleResources();
    createMeshResources();
    createGridResources();

    spdlog::debug("Initializing ImGui...");
    imgui_ = std::make_unique<ImGuiLayer>(window.handle(), *context_, *swapchain_);

    spdlog::info("Renderer initialized");
}

Renderer::~Renderer() {
    if (context_ && context_->device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_->device());
    }

    vertex_buffer_.reset();
    triangle_pipeline_.reset();
    mesh_pipeline_.reset();
    mesh_additive_pipeline_.reset();
    mesh_depth_pipeline_.reset();
    grid_pipeline_.reset();
    mesh_batch_drawer_.reset();
    display_lut_.reset();
    bindless_textures_.reset();
    imgui_.reset();
    frame_ubo_.reset();

    if (frame_descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_->device(), frame_descriptor_pool_, nullptr);
        frame_descriptor_pool_ = VK_NULL_HANDLE;
    }
    if (frame_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_->device(), frame_set_layout_, nullptr);
        frame_set_layout_ = VK_NULL_HANDLE;
    }

    for (auto fence : in_flight_fences_) {
        vkDestroyFence(context_->device(), fence, nullptr);
    }
    for (auto semaphore : render_finished_semaphores_) {
        vkDestroySemaphore(context_->device(), semaphore, nullptr);
    }
    for (auto semaphore : image_available_semaphores_) {
        vkDestroySemaphore(context_->device(), semaphore, nullptr);
    }

    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_->device(), command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }

    swapchain_.reset();
    context_.reset();
}

void Renderer::setFrameUniforms(const FrameUniforms& uniforms) {
    frame_uniforms_ = uniforms;
    if (!frame_ubo_) {
        return;
    }
    const VkDeviceSize offset = static_cast<VkDeviceSize>(current_frame_) * sizeof(FrameUniforms);
    frame_ubo_->upload(&uniforms, sizeof(FrameUniforms), offset);
}

void Renderer::setViewProjection(const float* mat4_col_major) {
    FrameUniforms u{};
    std::memcpy(u.view_proj, mat4_col_major, sizeof(u.view_proj));
    u.camera_pos[3] = 1.0f;
    // Match buildFrameUniforms with zero lights: no residual ambient.
    u.ibl_intensity = 0.0f;
    u.fog_density = 0.0f;
    u.ambient_sky[0] = 0.0f;
    u.ambient_sky[1] = 0.0f;
    u.ambient_sky[2] = 0.0f;
    u.ambient_ground[0] = 0.0f;
    u.ambient_ground[1] = 0.0f;
    u.ambient_ground[2] = 0.0f;
    u.display_params[0] = 0.0f;
    u.display_params[1] = 0.18f;
    u.display_params[2] = 0.22f;
    u.display_params[3] = 0.0f;
    u.fog_color[3] = 0.018f;
    u.light_count = 0;
    setFrameUniforms(u);
}

void Renderer::setClearColor(const float r, const float g, const float b, const float a) {
    clear_color_[0] = r;
    clear_color_[1] = g;
    clear_color_[2] = b;
    clear_color_[3] = a;
}

void Renderer::uploadDisplayLut(const ColorLut3D& lut) {
    if (display_lut_) {
        display_lut_->upload(lut);
    }
}

void Renderer::setViewportDrawRegion(const float x, const float y, const float width, const float height) {
    draw_region_enabled_ = width > 1.0f && height > 1.0f;
    draw_region_x_ = x;
    draw_region_y_ = y;
    draw_region_w_ = width;
    draw_region_h_ = height;
}

void Renderer::clearViewportDrawRegion() {
    draw_region_enabled_ = false;
}

void Renderer::applyDrawRegion(const VkCommandBuffer cmd) const {
    VkViewport viewport{};
    VkRect2D scissor{};

    if (draw_region_enabled_) {
        viewport.x = draw_region_x_;
        viewport.y = draw_region_y_;
        viewport.width = draw_region_w_;
        viewport.height = draw_region_h_;
        scissor.offset.x = static_cast<int32_t>(draw_region_x_);
        scissor.offset.y = static_cast<int32_t>(draw_region_y_);
        scissor.extent.width = static_cast<uint32_t>(draw_region_w_);
        scissor.extent.height = static_cast<uint32_t>(draw_region_h_);
    } else {
        viewport.width = static_cast<float>(swapchain_->extent().width);
        viewport.height = static_cast<float>(swapchain_->extent().height);
        scissor.offset = {0, 0};
        scissor.extent = swapchain_->extent();
    }

    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void Renderer::createFrameDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[2]{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 2;
    info.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(context_->device(), &info, nullptr, &frame_set_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create frame descriptor set layout");
    }
}

void Renderer::createFrameDescriptorPool() {
    VkDescriptorPoolSize pool_sizes[2]{};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = static_cast<uint32_t>(kFramesInFlight);
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sizes[1].descriptorCount = static_cast<uint32_t>(kFramesInFlight);

    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets = static_cast<uint32_t>(kFramesInFlight);
    info.poolSizeCount = 2;
    info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(context_->device(), &info, nullptr, &frame_descriptor_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create frame descriptor pool");
    }
}

void Renderer::createFrameDescriptorSets() {
    frame_descriptor_sets_.resize(kFramesInFlight);

    std::vector<VkDescriptorSetLayout> layouts(kFramesInFlight, frame_set_layout_);
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = frame_descriptor_pool_;
    alloc.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    alloc.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(context_->device(), &alloc, frame_descriptor_sets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate frame descriptor sets");
    }

    for (int i = 0; i < kFramesInFlight; ++i) {
        VkDescriptorBufferInfo buf{};
        buf.buffer = frame_ubo_->handle();
        buf.offset = static_cast<VkDeviceSize>(i) * sizeof(FrameUniforms);
        buf.range = sizeof(FrameUniforms);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = frame_descriptor_sets_[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &buf;

        vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
    }
}

void Renderer::createCommandPool() {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = context_->graphicsQueueFamily();

    if (vkCreateCommandPool(context_->device(), &pool_info, nullptr, &command_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }
}

void Renderer::createSyncObjects() {
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    image_available_semaphores_.resize(kFramesInFlight);
    render_finished_semaphores_.resize(kFramesInFlight);
    in_flight_fences_.resize(kFramesInFlight);

    for (int i = 0; i < kFramesInFlight; ++i) {
        if (vkCreateSemaphore(context_->device(), &semaphore_info, nullptr,
                              &image_available_semaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(context_->device(), &semaphore_info, nullptr,
                              &render_finished_semaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(context_->device(), &fence_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create synchronization objects");
        }
    }
}

void Renderer::createCommandBuffers() {
    command_buffers_.resize(kFramesInFlight);

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = static_cast<uint32_t>(command_buffers_.size());

    if (vkAllocateCommandBuffers(context_->device(), &alloc_info, command_buffers_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers");
    }
}

void Renderer::createTriangleResources() {
    const std::filesystem::path shader_dir = resolveShaderDirectory();

    if (!frame_set_layout_) {
        createFrameDescriptorSetLayout();
        createFrameDescriptorPool();
        const VkDeviceSize total_size = sizeof(FrameUniforms) * kFramesInFlight;
        frame_ubo_ = std::make_unique<Buffer>(
            *context_, total_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        createFrameDescriptorSets();
    }

    GraphicsPipelineConfig pipeline_config{};
    pipeline_config.render_pass = swapchain_->renderPass();
    pipeline_config.vert_shader = shader_dir / "triangle.vert.spv";
    pipeline_config.frag_shader = shader_dir / "triangle.frag.spv";
    pipeline_config.extent = swapchain_->extent();
    pipeline_config.set0_layout = frame_set_layout_;
    pipeline_config.depth_test = false;

    if (!std::filesystem::exists(pipeline_config.vert_shader)) {
        throw std::runtime_error("Missing shader: " + pipeline_config.vert_shader.string());
    }
    if (!std::filesystem::exists(pipeline_config.frag_shader)) {
        throw std::runtime_error("Missing shader: " + pipeline_config.frag_shader.string());
    }

    triangle_pipeline_ = std::make_unique<GraphicsPipeline>(*context_, pipeline_config);

    const VkDeviceSize buffer_size = sizeof(kTriangleVertices);
    vertex_buffer_ = std::make_unique<Buffer>(
        *context_, buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vertex_buffer_->upload(kTriangleVertices.data(), buffer_size);
}

void Renderer::createMeshResources() {
    bindless_textures_ = std::make_unique<BindlessTextures>(*context_);
    display_lut_ = std::make_unique<DisplayLutBinding>(*context_);
    mesh_batch_drawer_ = std::make_unique<MeshBatchDrawer>(*context_);

    for (VkDescriptorSet frame_set : frame_descriptor_sets_) {
        mesh_batch_drawer_->bindInstanceBuffer(context_->device(), frame_set);
    }

    createMeshPipelines();
}

void Renderer::createMeshPipelines() {
    mesh_pipeline_.reset();
    mesh_additive_pipeline_.reset();
    mesh_depth_pipeline_.reset();
    selection_outline_pipeline_.reset();
    if (!bindless_textures_ || !display_lut_) {
        return;
    }

    const std::filesystem::path shader_dir = resolveShaderDirectory();
    GraphicsPipelineConfig pipeline_config{};
    pipeline_config.render_pass = swapchain_->renderPass();
    pipeline_config.vert_shader = shader_dir / "mesh.vert.spv";
    pipeline_config.frag_shader = shader_dir / "mesh.frag.spv";
    pipeline_config.extent = swapchain_->extent();
    pipeline_config.set0_layout = frame_set_layout_;
    pipeline_config.set1_layout = bindless_textures_->layout();
    pipeline_config.set2_layout = display_lut_ ? display_lut_->layout() : VK_NULL_HANDLE;
    pipeline_config = withMeshVertexLayout(pipeline_config);
    pipeline_config.depth_test = true;
    pipeline_config.depth_write = false;
    pipeline_config.alpha_blend = true;
    pipeline_config.cull_mode = VK_CULL_MODE_NONE;
    pipeline_config.push_constant_size = sizeof(MeshMaterialPushConstants);
    pipeline_config.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    if (std::filesystem::exists(pipeline_config.vert_shader) && std::filesystem::exists(pipeline_config.frag_shader)) {
        mesh_pipeline_ = std::make_unique<GraphicsPipeline>(*context_, pipeline_config);
        GraphicsPipelineConfig additive_config = pipeline_config;
        additive_config.additive_blend = true;
        mesh_additive_pipeline_ = std::make_unique<GraphicsPipeline>(*context_, additive_config);
    }

    GraphicsPipelineConfig depth_config = pipeline_config;
    depth_config.frag_shader = shader_dir / "mesh_depth.frag.spv";
    depth_config.set2_layout = VK_NULL_HANDLE;
    depth_config.depth_write = true;
    depth_config.color_write_mask = 0;
    depth_config.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    if (std::filesystem::exists(depth_config.vert_shader) && std::filesystem::exists(depth_config.frag_shader)) {
        mesh_depth_pipeline_ = std::make_unique<GraphicsPipeline>(*context_, depth_config);
    }

    GraphicsPipelineConfig outline_config = withMeshVertexLayout({});
    outline_config.render_pass = swapchain_->renderPass();
    outline_config.vert_shader = shader_dir / "selection_outline.vert.spv";
    outline_config.frag_shader = shader_dir / "selection_outline.frag.spv";
    outline_config.extent = swapchain_->extent();
    outline_config.set0_layout = frame_set_layout_;
    // Layout matches MeshBatchDrawer binds (set0 + bindless); frag does not sample.
    outline_config.set1_layout = bindless_textures_->layout();
    // Depth-test off: rim draws on top of the actor (lavapipe equal-depth is flaky).
    outline_config.depth_test = false;
    outline_config.depth_write = false;
    outline_config.alpha_blend = true;
    // Fresnel rim on both faces — readable silhouette without inverted-hull winding issues.
    outline_config.cull_mode = VK_CULL_MODE_NONE;
    outline_config.push_constant_size = sizeof(MeshMaterialPushConstants);
    outline_config.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    if (std::filesystem::exists(outline_config.vert_shader) &&
        std::filesystem::exists(outline_config.frag_shader)) {
        selection_outline_pipeline_ = std::make_unique<GraphicsPipeline>(*context_, outline_config);
    }
}

void Renderer::createGridResources() {
    const std::filesystem::path shader_dir = resolveShaderDirectory();
    GraphicsPipelineConfig pipeline_config{};
    pipeline_config.render_pass = swapchain_->renderPass();
    pipeline_config.vert_shader = shader_dir / "grid.vert.spv";
    pipeline_config.frag_shader = shader_dir / "grid.frag.spv";
    pipeline_config.extent = swapchain_->extent();
    pipeline_config.set0_layout = frame_set_layout_;
    // Triangle ground plane + procedural lines in the fragment shader.
    pipeline_config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipeline_config.vertex_stride = sizeof(float) * 6;
    pipeline_config.vertex_attributes = {
        {0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)},
    };
    pipeline_config.depth_test = true;
    pipeline_config.depth_write = false;
    pipeline_config.alpha_blend = true;
    pipeline_config.cull_mode = VK_CULL_MODE_NONE;
    pipeline_config.push_constant_size = sizeof(GridDrawPushConstants);
    pipeline_config.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    if (std::filesystem::exists(pipeline_config.vert_shader) && std::filesystem::exists(pipeline_config.frag_shader)) {
        grid_pipeline_ = std::make_unique<GraphicsPipeline>(*context_, pipeline_config);
    }
}

void Renderer::recreateSwapchain(int width, int height) {
    if (width == 0 || height == 0) {
        return;
    }

    vkDeviceWaitIdle(context_->device());
    swapchain_->recreate(width, height);

    triangle_pipeline_.reset();
    GraphicsPipelineConfig pipeline_config{};
    pipeline_config.render_pass = swapchain_->renderPass();
    pipeline_config.vert_shader = resolveShaderDirectory() / "triangle.vert.spv";
    pipeline_config.frag_shader = resolveShaderDirectory() / "triangle.frag.spv";
    pipeline_config.extent = swapchain_->extent();
    pipeline_config.set0_layout = frame_set_layout_;
    triangle_pipeline_ = std::make_unique<GraphicsPipeline>(*context_, pipeline_config);

    if (bindless_textures_) {
        createMeshPipelines();
        if (mesh_batch_drawer_) {
            for (VkDescriptorSet frame_set : frame_descriptor_sets_) {
                mesh_batch_drawer_->bindInstanceBuffer(context_->device(), frame_set);
            }
        }
    }

    if (grid_pipeline_) {
        grid_pipeline_.reset();
        GraphicsPipelineConfig grid_cfg{};
        grid_cfg.render_pass = swapchain_->renderPass();
        grid_cfg.vert_shader = resolveShaderDirectory() / "grid.vert.spv";
        grid_cfg.frag_shader = resolveShaderDirectory() / "grid.frag.spv";
        grid_cfg.extent = swapchain_->extent();
        grid_cfg.set0_layout = frame_set_layout_;
        grid_cfg.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        grid_cfg.vertex_stride = sizeof(float) * 6;
        grid_cfg.vertex_attributes = {
            {0, VK_FORMAT_R32G32B32_SFLOAT, 0},
            {1, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)},
        };
        grid_cfg.depth_test = true;
        grid_cfg.depth_write = false;
        grid_cfg.alpha_blend = true;
        grid_cfg.cull_mode = VK_CULL_MODE_NONE;
        grid_cfg.push_constant_size = sizeof(GridDrawPushConstants);
        grid_cfg.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        if (std::filesystem::exists(grid_cfg.vert_shader) && std::filesystem::exists(grid_cfg.frag_shader)) {
            grid_pipeline_ = std::make_unique<GraphicsPipeline>(*context_, grid_cfg);
        }
    }

    imgui_.reset();
    imgui_ = std::make_unique<ImGuiLayer>(window_.handle(), *context_, *swapchain_);

    framebuffer_resized_ = false;
}

void Renderer::onResize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (swapchain_ && swapchain_->extent().width == static_cast<std::uint32_t>(width) &&
        swapchain_->extent().height == static_cast<std::uint32_t>(height)) {
        framebuffer_resized_ = false;
        return;
    }
    framebuffer_resized_ = true;
    recreateSwapchain(width, height);
}

void Renderer::waitIdle() {
    if (context_ && context_->device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_->device());
    }
}

void Renderer::enableImGuiLayoutPersistence() {
    if (imgui_) {
        imgui_->enableLayoutPersistence();
    }
}

void Renderer::ensureRenderPass() {
    if (render_pass_active_) {
        return;
    }

    VkCommandBuffer cmd = command_buffers_[current_frame_];

    VkClearValue clear_color{};
    clear_color.color = {{clear_color_[0], clear_color_[1], clear_color_[2], clear_color_[3]}};

    VkClearValue clear_depth{};
    clear_depth.depthStencil = {1.0f, 0};

    VkClearValue clears[2] = {clear_color, clear_depth};

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = swapchain_->renderPass();
    render_pass_info.framebuffer = swapchain_->framebuffers()[image_index_];
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = swapchain_->extent();
    render_pass_info.clearValueCount = 2;
    render_pass_info.pClearValues = clears;

    vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    render_pass_active_ = true;
}

void Renderer::beginFrame() {
    if (window_.wasResized()) {
        onResize(window_.width(), window_.height());
        window_.clearResizedFlag();
    }

    vkWaitForFences(context_->device(), 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);

    image_index_ = swapchain_->acquireNextImage(image_available_semaphores_[current_frame_]);

    vkResetFences(context_->device(), 1, &in_flight_fences_[current_frame_]);

    VkCommandBuffer cmd = command_buffers_[current_frame_];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin command buffer");
    }

    render_pass_active_ = false;
    if (mesh_batch_drawer_) {
        mesh_batch_drawer_->resetFrame();
    }
}

void Renderer::drawTriangle() {
    ensureRenderPass();
    VkCommandBuffer cmd = command_buffers_[current_frame_];
    applyDrawRegion(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, triangle_pipeline_->handle());

    if (!frame_descriptor_sets_.empty()) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, triangle_pipeline_->layout(), 0, 1,
                                &frame_descriptor_sets_[current_frame_], 0, nullptr);
    }

    const VkBuffer buffers[] = {vertex_buffer_->handle()};
    const VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void Renderer::drawMesh(const Mesh& mesh, const MeshDrawPushConstants& draw_constants,
                        const GpuTexture* base_color_texture) {
    SceneMeshBatch batch{};
    batch.mesh = &mesh;
    batch.base_color_texture = base_color_texture;
    std::memcpy(batch.material.base_color, draw_constants.base_color, sizeof(batch.material.base_color));
    std::memcpy(batch.material.emissive, draw_constants.emissive, sizeof(batch.material.emissive));
    std::memcpy(batch.material.material_params, draw_constants.material_params,
                sizeof(batch.material.material_params));

    GpuMeshInstance instance{};
    glm::mat4 model{};
    std::memcpy(&model[0][0], draw_constants.model, sizeof(draw_constants.model));
    fillGpuMeshInstance(instance, model, mesh.local_aabb_min, mesh.local_aabb_max);
    batch.instances.push_back(instance);
    drawMeshBatches({batch});
}

void Renderer::drawMeshBatches(const std::vector<SceneMeshBatch>& batches, const MeshDrawOptions& options) {
    if (!mesh_pipeline_ || !bindless_textures_ || !mesh_batch_drawer_ || batches.empty()) {
        return;
    }

    VkCommandBuffer cmd = command_buffers_[current_frame_];

    MeshDrawOptions draw_options = options;
    if (draw_options.view_proj_col_major == nullptr) {
        draw_options.view_proj_col_major = frame_uniforms_.view_proj;
    }

    std::vector<SceneMeshBatch> alpha_batches;
    std::vector<SceneMeshBatch> additive_batches;
    alpha_batches.reserve(batches.size());
    additive_batches.reserve(batches.size());
    for (const SceneMeshBatch& batch : batches) {
        if (batch.additive) {
            additive_batches.push_back(batch);
        } else {
            alpha_batches.push_back(batch);
        }
    }

    auto draw_group = [&](const std::vector<SceneMeshBatch>& group, GraphicsPipeline& pipeline,
                          bool depth_prepass) {
        if (group.empty()) {
            return;
        }
        if (!mesh_batch_drawer_->prepareDraw(group, *bindless_textures_, draw_options)) {
            return;
        }
        mesh_batch_drawer_->dispatchGpuCull(cmd);
        mesh_batch_drawer_->bindDrawInstanceBuffer(context_->device(),
                                                   frame_descriptor_sets_[current_frame_]);
        ensureRenderPass();
        applyDrawRegion(cmd);
        if (depth_prepass && draw_options.enable_depth_prepass && mesh_depth_pipeline_) {
            mesh_batch_drawer_->drawDepthPrepass(cmd, *mesh_depth_pipeline_,
                                                 frame_descriptor_sets_[current_frame_], *bindless_textures_);
        }
        mesh_batch_drawer_->drawColorPass(cmd, pipeline, frame_descriptor_sets_[current_frame_],
                                          *bindless_textures_,
                                          display_lut_ ? display_lut_->descriptorSet() : VK_NULL_HANDLE);
    };

    draw_group(alpha_batches, *mesh_pipeline_, true);
    if (mesh_additive_pipeline_) {
        // Additive particles skip depth-prepass writes (already depth-tested, no write).
        draw_group(additive_batches, *mesh_additive_pipeline_, false);
    } else {
        draw_group(additive_batches, *mesh_pipeline_, false);
    }
}

void Renderer::drawSelectionOutlineBatches(const std::vector<SceneMeshBatch>& batches,
                                           const MeshDrawOptions& options) {
    if (!selection_outline_pipeline_) {
        spdlog::warn("selection outline pipeline missing");
        return;
    }
    if (!bindless_textures_ || !mesh_batch_drawer_ || batches.empty()) {
        return;
    }

    VkCommandBuffer cmd = command_buffers_[current_frame_];
    MeshDrawOptions draw_options = options;
    if (draw_options.view_proj_col_major == nullptr) {
        draw_options.view_proj_col_major = frame_uniforms_.view_proj;
    }
    draw_options.enable_gpu_cull = false;
    draw_options.enable_depth_prepass = false;

    if (!mesh_batch_drawer_->prepareDraw(batches, *bindless_textures_, draw_options)) {
        return;
    }
    mesh_batch_drawer_->bindDrawInstanceBuffer(context_->device(),
                                               frame_descriptor_sets_[current_frame_]);
    ensureRenderPass();
    applyDrawRegion(cmd);
    mesh_batch_drawer_->drawColorPass(cmd, *selection_outline_pipeline_,
                                      frame_descriptor_sets_[current_frame_], *bindless_textures_,
                                      VK_NULL_HANDLE);
}

void Renderer::drawGrid(const Mesh& mesh, const GridDrawPushConstants& draw_constants) {
    if (!grid_pipeline_) {
        return;
    }

    ensureRenderPass();
    VkCommandBuffer cmd = command_buffers_[current_frame_];
    applyDrawRegion(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, grid_pipeline_->handle());

    if (!frame_descriptor_sets_.empty()) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, grid_pipeline_->layout(), 0, 1,
                                &frame_descriptor_sets_[current_frame_], 0, nullptr);
    }

    vkCmdPushConstants(cmd, grid_pipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(GridDrawPushConstants), &draw_constants);

    VkBuffer vb = mesh.vertex_buffer->handle();
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &off);
    vkCmdBindIndexBuffer(cmd, mesh.index_buffer->handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mesh.index_count, 1, 0, 0, 0);
}

void Renderer::beginUiFrame() {
    imgui_->beginFrame();
}

void Renderer::endFrame() {
    VkCommandBuffer cmd = command_buffers_[current_frame_];

    if (!render_pass_active_) {
        ensureRenderPass();
    }

    imgui_->endFrame(cmd);

    if (render_pass_active_) {
        vkCmdEndRenderPass(cmd);
        render_pass_active_ = false;
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer");
    }

    VkSemaphore wait_semaphores[] = {image_available_semaphores_[current_frame_]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signal_semaphores[] = {render_finished_semaphores_[current_frame_]};

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    if (vkQueueSubmit(context_->graphicsQueue(), 1, &submit_info, in_flight_fences_[current_frame_]) !=
        VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    swapchain_->present(image_index_, render_finished_semaphores_[current_frame_]);
    current_frame_ = (current_frame_ + 1) % kFramesInFlight;
}

VkInstance Renderer::instance() const {
    return context_->instance();
}

const VulkanContext& Renderer::context() const {
    return *context_;
}

const char* Renderer::deviceName() const {
    return device_name_.c_str();
}

} // namespace genesis
