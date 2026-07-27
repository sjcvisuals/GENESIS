#include "renderer/OffscreenRenderer.hpp"

#include "platform/Paths.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/MeshBatchDrawer.hpp"
#include "renderer/MeshInstance.hpp"
#include "renderer/BindlessTextures.hpp"
#include "renderer/DisplayLutBinding.hpp"
#include "color/ColorManagement.hpp"
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

OffscreenRenderer::OffscreenRenderer(const OffscreenRendererConfig& config) : owns_context_(true) {
    VulkanContextConfig context_config{};
    context_config.window = nullptr;
    context_config.adapter_luid = config.adapter_luid;
    context_config.enable_validation = config.enable_validation;

    owned_context_ = std::make_unique<VulkanContext>(context_config);
    context_ = owned_context_.get();

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

OffscreenRenderer::OffscreenRenderer(VulkanContext& shared_context, const std::uint32_t width,
                                     const std::uint32_t height)
    : context_(&shared_context), owns_context_(false) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(context_->physicalDevice(), &props);
    device_name_ = props.deviceName;

    ExternalRenderTargetConfig target_config{};
    target_config.width = width;
    target_config.height = height;
    target_ = std::make_unique<ExternalRenderTarget>(*context_, target_config);
    sync_ = std::make_unique<ExternalSync>(*context_);

    createCommandPool();
    createSyncObjects();
    createCommandBuffers();
    createMeshPipeline();

    spdlog::info("OffscreenRenderer shared output initialized ({}x{})", width, height);
}

OffscreenRenderer::~OffscreenRenderer() {
    if (context_ && context_->device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_->device());
    }

    mesh_pipeline_.reset();
    mesh_depth_pipeline_.reset();
    bindless_textures_.reset();
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
    owned_context_.reset();
    context_ = nullptr;
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
    bindless_textures_ = std::make_unique<BindlessTextures>(*context_);
    display_lut_ = std::make_unique<DisplayLutBinding>(*context_);

    VkDescriptorSetLayoutBinding bindings[2]{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 2;
    layout_info.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(context_->device(), &layout_info, nullptr, &frame_set_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen descriptor set layout");
    }

    VkDescriptorPoolSize pool_sizes[2]{};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = static_cast<uint32_t>(kFramesInFlight);
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sizes[1].descriptorCount = static_cast<uint32_t>(kFramesInFlight);

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = static_cast<uint32_t>(kFramesInFlight);
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;

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

    mesh_batch_drawer_ = std::make_unique<MeshBatchDrawer>(*context_);
    for (VkDescriptorSet frame_set : frame_descriptor_sets_) {
        mesh_batch_drawer_->bindInstanceBuffer(context_->device(), frame_set);
    }

    createMeshPipelines();
}

void OffscreenRenderer::createMeshPipelines() {
    mesh_pipeline_.reset();
    mesh_depth_pipeline_.reset();
    if (!bindless_textures_ || !display_lut_) {
        return;
    }

    const std::filesystem::path shader_dir = resolveShaderDirectory();
    GraphicsPipelineConfig pipeline_config{};
    pipeline_config.render_pass = target_->renderPass();
    pipeline_config.vert_shader = shader_dir / "mesh.vert.spv";
    pipeline_config.frag_shader = shader_dir / "mesh.frag.spv";
    pipeline_config.extent = target_->extent();
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

    if (std::filesystem::exists(pipeline_config.vert_shader) &&
        std::filesystem::exists(pipeline_config.frag_shader)) {
        mesh_pipeline_ = std::make_unique<GraphicsPipeline>(*context_, pipeline_config);
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
}

void OffscreenRenderer::recreatePipelines() {
    createMeshPipelines();
}

void OffscreenRenderer::setFrameUniforms(const FrameUniforms& uniforms) {
    frame_uniforms_ = uniforms;
    if (!frame_ubo_) {
        return;
    }
    const VkDeviceSize offset = static_cast<VkDeviceSize>(current_frame_) * sizeof(FrameUniforms);
    frame_ubo_->upload(&uniforms, sizeof(FrameUniforms), offset);
}

void OffscreenRenderer::setClearColor(const float r, const float g, const float b, const float a) {
    clear_color_[0] = r;
    clear_color_[1] = g;
    clear_color_[2] = b;
    clear_color_[3] = a;
}

void OffscreenRenderer::uploadDisplayLut(const ColorLut3D& lut) {
    if (display_lut_) {
        display_lut_->upload(lut);
    }
}

void OffscreenRenderer::ensureRenderPass() {
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
    render_pass_info.renderPass = target_->renderPass();
    render_pass_info.framebuffer = target_->framebuffer();
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = target_->extent();
    render_pass_info.clearValueCount = 2;
    render_pass_info.pClearValues = clears;

    vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    render_pass_active_ = true;
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

    render_pass_active_ = false;
    if (mesh_batch_drawer_) {
        mesh_batch_drawer_->resetFrame();
    }
}

void OffscreenRenderer::drawMesh(const Mesh& mesh, const MeshDrawPushConstants& draw_constants,
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

void OffscreenRenderer::drawMeshBatches(const std::vector<SceneMeshBatch>& batches,
                                        const MeshDrawOptions& options) {
    if (!mesh_pipeline_ || !bindless_textures_ || !mesh_batch_drawer_ || batches.empty()) {
        return;
    }

    VkCommandBuffer cmd = command_buffers_[current_frame_];

    MeshDrawOptions draw_options = options;
    if (draw_options.view_proj_col_major == nullptr) {
        draw_options.view_proj_col_major = frame_uniforms_.view_proj;
    }

    if (!mesh_batch_drawer_->prepareDraw(batches, *bindless_textures_, draw_options)) {
        return;
    }

    mesh_batch_drawer_->dispatchGpuCull(cmd);
    mesh_batch_drawer_->bindDrawInstanceBuffer(context_->device(), frame_descriptor_sets_[current_frame_]);
    ensureRenderPass();

    VkViewport viewport{};
    viewport.width = static_cast<float>(target_->extent().width);
    viewport.height = static_cast<float>(target_->extent().height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, target_->extent()};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (draw_options.enable_depth_prepass && mesh_depth_pipeline_) {
        mesh_batch_drawer_->drawDepthPrepass(cmd, *mesh_depth_pipeline_, frame_descriptor_sets_[current_frame_],
                                             *bindless_textures_);
    }
    mesh_batch_drawer_->drawColorPass(cmd, *mesh_pipeline_, frame_descriptor_sets_[current_frame_],
                                      *bindless_textures_,
                                      display_lut_ ? display_lut_->descriptorSet() : VK_NULL_HANDLE);
}

void OffscreenRenderer::endFrame(VkSemaphore wait_host_finished) {
    VkCommandBuffer cmd = command_buffers_[current_frame_];
    if (render_pass_active_) {
        vkCmdEndRenderPass(cmd);
        render_pass_active_ = false;
    }

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

void OffscreenRenderer::waitFrameComplete() {
    const size_t completed_frame = (current_frame_ + kFramesInFlight - 1) % kFramesInFlight;
    vkWaitForFences(context_->device(), 1, &in_flight_fences_[completed_frame], VK_TRUE, UINT64_MAX);
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
