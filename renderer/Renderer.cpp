#include "renderer/Renderer.hpp"

#include "renderer/FrameUniforms.hpp"
#include "renderer/GridDrawPushConstants.hpp"
#include "renderer/MeshDrawPushConstants.hpp"

#include "platform/ImGuiLayer.hpp"
#include "platform/Window.hpp"
#include "renderer/vulkan/Buffer.hpp"
#include "renderer/vulkan/Pipeline.hpp"
#include "renderer/vulkan/Swapchain.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/MaterialBinding.hpp"
#include "renderer/Mesh.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <array>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <cstring>

namespace genesis {

namespace {

struct Vertex {
    float position[3];
    float color[3];
};

const std::array<Vertex, 3> kTriangleVertices = {{
    {{0.0f, -0.5f, 0.0f}, {1.0f, 0.2f, 0.3f}},
    {{0.5f, 0.5f, 0.0f}, {0.2f, 1.0f, 0.4f}},
    {{-0.5f, 0.5f, 0.0f}, {0.3f, 0.4f, 1.0f}},
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
    grid_pipeline_.reset();
    material_binding_.reset();
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
    }

    swapchain_.reset();
    context_.reset();
}

void Renderer::setFrameUniforms(const FrameUniforms& uniforms) {
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
    u.light_count = 0;
    setFrameUniforms(u);
}

void Renderer::createFrameDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding ubo_binding{};
    ubo_binding.binding = 0;
    ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings = &ubo_binding;

    if (vkCreateDescriptorSetLayout(context_->device(), &info, nullptr, &frame_set_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create frame descriptor set layout");
    }
}

void Renderer::createFrameDescriptorPool() {
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_size.descriptorCount = static_cast<uint32_t>(kFramesInFlight);

    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets = static_cast<uint32_t>(kFramesInFlight);
    info.poolSizeCount = 1;
    info.pPoolSizes = &pool_size;

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
    const std::filesystem::path shader_dir = GENESIS_SHADER_DIR;

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
    material_binding_ = std::make_unique<MaterialBinding>(*context_);

    const std::filesystem::path shader_dir = GENESIS_SHADER_DIR;
    GraphicsPipelineConfig pipeline_config{};
    pipeline_config.render_pass = swapchain_->renderPass();
    pipeline_config.vert_shader = shader_dir / "mesh.vert.spv";
    pipeline_config.frag_shader = shader_dir / "mesh.frag.spv";
    pipeline_config.extent = swapchain_->extent();
    pipeline_config.set0_layout = frame_set_layout_;
    pipeline_config.set1_layout = material_binding_->layout();
    pipeline_config = withMeshVertexLayout(pipeline_config);
    pipeline_config.depth_test = true;
    pipeline_config.push_constant_size = sizeof(MeshDrawPushConstants);
    pipeline_config.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    // Mesh pipeline is optional: it will only be used by the glTF viewer example.
    if (std::filesystem::exists(pipeline_config.vert_shader) && std::filesystem::exists(pipeline_config.frag_shader)) {
        mesh_pipeline_ = std::make_unique<GraphicsPipeline>(*context_, pipeline_config);
    }
}

void Renderer::createGridResources() {
    const std::filesystem::path shader_dir = GENESIS_SHADER_DIR;
    GraphicsPipelineConfig pipeline_config{};
    pipeline_config.render_pass = swapchain_->renderPass();
    pipeline_config.vert_shader = shader_dir / "grid.vert.spv";
    pipeline_config.frag_shader = shader_dir / "grid.frag.spv";
    pipeline_config.extent = swapchain_->extent();
    pipeline_config.set0_layout = frame_set_layout_;
    pipeline_config.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    pipeline_config.vertex_stride = sizeof(float) * 6;
    pipeline_config.vertex_attributes = {
        {0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)},
    };
    pipeline_config.depth_test = true;
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
    pipeline_config.vert_shader = std::filesystem::path(GENESIS_SHADER_DIR) / "triangle.vert.spv";
    pipeline_config.frag_shader = std::filesystem::path(GENESIS_SHADER_DIR) / "triangle.frag.spv";
    pipeline_config.extent = swapchain_->extent();
    pipeline_config.set0_layout = frame_set_layout_;
    triangle_pipeline_ = std::make_unique<GraphicsPipeline>(*context_, pipeline_config);

    // Recreate mesh pipeline if present.
    if (mesh_pipeline_ && material_binding_) {
        mesh_pipeline_.reset();
        GraphicsPipelineConfig mesh_cfg{};
        mesh_cfg.render_pass = swapchain_->renderPass();
        mesh_cfg.vert_shader = std::filesystem::path(GENESIS_SHADER_DIR) / "mesh.vert.spv";
        mesh_cfg.frag_shader = std::filesystem::path(GENESIS_SHADER_DIR) / "mesh.frag.spv";
        mesh_cfg.extent = swapchain_->extent();
        mesh_cfg.set0_layout = frame_set_layout_;
        mesh_cfg.set1_layout = material_binding_->layout();
        mesh_cfg = withMeshVertexLayout(mesh_cfg);
        mesh_cfg.depth_test = true;
        mesh_cfg.push_constant_size = sizeof(MeshDrawPushConstants);
        mesh_cfg.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        if (std::filesystem::exists(mesh_cfg.vert_shader) && std::filesystem::exists(mesh_cfg.frag_shader)) {
            mesh_pipeline_ = std::make_unique<GraphicsPipeline>(*context_, mesh_cfg);
        }
    }

    if (grid_pipeline_) {
        grid_pipeline_.reset();
        GraphicsPipelineConfig grid_cfg{};
        grid_cfg.render_pass = swapchain_->renderPass();
        grid_cfg.vert_shader = std::filesystem::path(GENESIS_SHADER_DIR) / "grid.vert.spv";
        grid_cfg.frag_shader = std::filesystem::path(GENESIS_SHADER_DIR) / "grid.frag.spv";
        grid_cfg.extent = swapchain_->extent();
        grid_cfg.set0_layout = frame_set_layout_;
        grid_cfg.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        grid_cfg.vertex_stride = sizeof(float) * 6;
        grid_cfg.vertex_attributes = {
            {0, VK_FORMAT_R32G32B32_SFLOAT, 0},
            {1, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)},
        };
        grid_cfg.depth_test = true;
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
    framebuffer_resized_ = true;
    recreateSwapchain(width, height);
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

    VkClearValue clear_color{};
    clear_color.color = {{0.02f, 0.02f, 0.03f, 1.0f}};

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
}

void Renderer::drawTriangle() {
    VkCommandBuffer cmd = command_buffers_[current_frame_];

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain_->extent().width);
    viewport.height = static_cast<float>(swapchain_->extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_->extent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

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
    if (!mesh_pipeline_ || !material_binding_) {
        return;
    }

    VkCommandBuffer cmd = command_buffers_[current_frame_];

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain_->extent().width);
    viewport.height = static_cast<float>(swapchain_->extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_->extent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipeline_->handle());

    const VkDescriptorSet material_set = material_binding_->descriptorFor(base_color_texture);
    const VkDescriptorSet sets[] = {frame_descriptor_sets_[current_frame_], material_set};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipeline_->layout(), 0, 2, sets, 0,
                            nullptr);

    vkCmdPushConstants(cmd, mesh_pipeline_->layout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(MeshDrawPushConstants), &draw_constants);

    VkBuffer vb = mesh.vertex_buffer->handle();
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &off);
    vkCmdBindIndexBuffer(cmd, mesh.index_buffer->handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mesh.index_count, 1, 0, 0, 0);
}

void Renderer::drawGrid(const Mesh& mesh, const GridDrawPushConstants& draw_constants) {
    if (!grid_pipeline_) {
        return;
    }

    VkCommandBuffer cmd = command_buffers_[current_frame_];

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain_->extent().width);
    viewport.height = static_cast<float>(swapchain_->extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_->extent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

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

    imgui_->endFrame(cmd);

    vkCmdEndRenderPass(cmd);

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
