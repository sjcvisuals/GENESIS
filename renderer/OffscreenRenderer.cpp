#include "renderer/OffscreenRenderer.hpp"

#include "renderer/Mesh.hpp"
#include "renderer/MaterialBinding.hpp"
#include "renderer/vulkan/Buffer.hpp"
#include "renderer/vulkan/ExternalRenderTarget.hpp"
#include "renderer/vulkan/ExternalSync.hpp"
#include "renderer/vulkan/Pipeline.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

#include <spdlog/spdlog.h>

#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace genesis {

OffscreenRenderer::OffscreenRenderer(const OffscreenRendererConfig& config) {
    VulkanContextConfig context_config{};
    context_config.window = nullptr;
    context_config.adapter_luid = config.adapter_luid;
    context_config.enable_validation = config.enable_validation;

    context_ = std::make_unique<VulkanContext>(context_config);

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(context_->physicalDevice(), &props);
    device_name_ = props.deviceName;

    ExternalRenderTargetConfig target_config{};
    target_config.width = config.width;
    target_config.height = config.height;
    target_ = std::make_unique<ExternalRenderTarget>(*context_, target_config);
    sync_ = std::make_unique<ExternalSync>(*context_);

    createCommandPool();
    createSyncObjects();
    createCommandBuffers();
    createMeshPipeline();

    spdlog::info("OffscreenRenderer initialized ({}x{})", config.width, config.height);
}

OffscreenRenderer::~OffscreenRenderer() {
    if (context_ && context_->device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_->device());
    }

    mesh_pipeline_.reset();
    material_binding_.reset();
    frame_ubo_.reset();

    if (frame_descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_->device(), frame_descriptor_pool_, nullptr);
    }
    if (frame_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_->device(), frame_set_layout_, nullptr);
    }

    for (VkFence fence : in_flight_fences_) {
        vkDestroyFence(context_->device(), fence, nullptr);
    }

    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_->device(), command_pool_, nullptr);
    }

    sync_.reset();
    target_.reset();
    context_.reset();
}

void OffscreenRenderer::resize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    target_->recreate(width, height);
    recreatePipelines();
}

void OffscreenRenderer::createCommandPool() {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = context_->graphicsQueueFamily();

    if (vkCreateCommandPool(context_->device(), &pool_info, nullptr, &command_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen command pool");
    }
}

void OffscreenRenderer::createSyncObjects() {
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    in_flight_fences_.resize(kFramesInFlight);
    for (int i = 0; i < kFramesInFlight; ++i) {
        if (vkCreateFence(context_->device(), &fence_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create offscreen fence");
        }
    }
}

void OffscreenRenderer::createCommandBuffers() {
    command_buffers_.resize(kFramesInFlight);

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = static_cast<uint32_t>(command_buffers_.size());

    if (vkAllocateCommandBuffers(context_->device(), &alloc_info, command_buffers_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate offscreen command buffers");
    }
}

void OffscreenRenderer::createMeshPipeline() {
    material_binding_ = std::make_unique<MaterialBinding>(*context_);

    VkDescriptorSetLayoutBinding ubo_binding{};
    ubo_binding.binding = 0;
    ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &ubo_binding;

    if (vkCreateDescriptorSetLayout(context_->device(), &layout_info, nullptr, &frame_set_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen descriptor set layout");
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_size.descriptorCount = static_cast<uint32_t>(kFramesInFlight);

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = static_cast<uint32_t>(kFramesInFlight);
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;

    if (vkCreateDescriptorPool(context_->device(), &pool_info, nullptr, &frame_descriptor_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen descriptor pool");
    }

    frame_descriptor_sets_.resize(kFramesInFlight);
    std::vector<VkDescriptorSetLayout> layouts(kFramesInFlight, frame_set_layout_);
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = frame_descriptor_pool_;
    alloc.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    alloc.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(context_->device(), &alloc, frame_descriptor_sets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate offscreen descriptor sets");
    }

    const VkDeviceSize total_size = sizeof(FrameUniforms) * kFramesInFlight;
    frame_ubo_ = std::make_unique<Buffer>(*context_, total_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

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

    recreatePipelines();
}

void OffscreenRenderer::recreatePipelines() {
    mesh_pipeline_.reset();

    const std::filesystem::path shader_dir = GENESIS_SHADER_DIR;
    GraphicsPipelineConfig pipeline_config{};
    pipeline_config.render_pass = target_->renderPass();
    pipeline_config.vert_shader = shader_dir / "mesh.vert.spv";
    pipeline_config.frag_shader = shader_dir / "mesh.frag.spv";
    pipeline_config.extent = target_->extent();
    pipeline_config.set0_layout = frame_set_layout_;
    pipeline_config.set1_layout = material_binding_->layout();
    pipeline_config = withMeshVertexLayout(pipeline_config);
    pipeline_config.depth_test = true;
    pipeline_config.push_constant_size = sizeof(MeshDrawPushConstants);
    pipeline_config.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    if (std::filesystem::exists(pipeline_config.vert_shader) &&
        std::filesystem::exists(pipeline_config.frag_shader)) {
        mesh_pipeline_ = std::make_unique<GraphicsPipeline>(*context_, pipeline_config);
    }
}

void OffscreenRenderer::setFrameUniforms(const FrameUniforms& uniforms) {
    if (!frame_ubo_) {
        return;
    }
    const VkDeviceSize offset = static_cast<VkDeviceSize>(current_frame_) * sizeof(FrameUniforms);
    frame_ubo_->upload(&uniforms, sizeof(FrameUniforms), offset);
}

void OffscreenRenderer::beginFrame() {
    vkWaitForFences(context_->device(), 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);
    vkResetFences(context_->device(), 1, &in_flight_fences_[current_frame_]);

    VkCommandBuffer cmd = command_buffers_[current_frame_];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin offscreen command buffer");
    }

    VkClearValue clear_color{};
    clear_color.color = {{0.02f, 0.02f, 0.03f, 1.0f}};
    VkClearValue clear_depth{};
    clear_depth.depthStencil = {1.0f, 0};
    VkClearValue clears[2] = {clear_color, clear_depth};

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = target_->renderPass();
    render_pass_info.framebuffer = target_->framebuffer();
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = target_->extent();
    render_pass_info.clearValueCount = 2;
    render_pass_info.pClearValues = clears;

    vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
}

void OffscreenRenderer::drawMesh(const Mesh& mesh, const MeshDrawPushConstants& draw_constants,
                                 const GpuTexture* base_color_texture) {
    if (!mesh_pipeline_ || !material_binding_) {
        return;
    }

    VkCommandBuffer cmd = command_buffers_[current_frame_];

    VkViewport viewport{};
    viewport.width = static_cast<float>(target_->extent().width);
    viewport.height = static_cast<float>(target_->extent().height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, target_->extent()};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipeline_->handle());

    const VkDescriptorSet material_set = material_binding_->descriptorFor(base_color_texture);
    const VkDescriptorSet sets[] = {frame_descriptor_sets_[current_frame_], material_set};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipeline_->layout(), 0, 2, sets, 0,
                            nullptr);

    vkCmdPushConstants(cmd, mesh_pipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(MeshDrawPushConstants), &draw_constants);

    VkBuffer vb = mesh.vertex_buffer->handle();
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &off);
    vkCmdBindIndexBuffer(cmd, mesh.index_buffer->handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mesh.index_count, 1, 0, 0, 0);
}

void OffscreenRenderer::endFrame(VkSemaphore wait_host_finished) {
    VkCommandBuffer cmd = command_buffers_[current_frame_];
    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record offscreen command buffer");
    }

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphore signal_semaphore = sync_->renderFinishedSemaphore();
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &signal_semaphore;

    if (wait_host_finished != VK_NULL_HANDLE) {
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &wait_host_finished;
        submit_info.pWaitDstStageMask = &wait_stage;
    }

    if (vkQueueSubmit(context_->graphicsQueue(), 1, &submit_info, in_flight_fences_[current_frame_]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit offscreen command buffer");
    }

    current_frame_ = (current_frame_ + 1) % kFramesInFlight;
}

const VulkanContext& OffscreenRenderer::context() const {
    return *context_;
}

const ExternalRenderTarget& OffscreenRenderer::target() const {
    return *target_;
}

const ExternalSync& OffscreenRenderer::sync() const {
    return *sync_;
}

const char* OffscreenRenderer::deviceName() const {
    return device_name_.c_str();
}

} // namespace genesis
