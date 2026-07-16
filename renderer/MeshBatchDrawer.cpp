#include "renderer/MeshBatchDrawer.hpp"

#include "platform/Paths.hpp"
#include "renderer/vulkan/Buffer.hpp"
#include "renderer/vulkan/ComputePipeline.hpp"
#include "renderer/vulkan/Pipeline.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "scene/Frustum.hpp"

#include <cstring>
#include <stdexcept>

namespace genesis {

namespace {

struct CullPushConstants {
    alignas(16) glm::vec4 frustum_planes[6]{};
    std::uint32_t batch_count = 0;
    std::uint32_t total_instances = 0;
    std::uint32_t _pad0 = 0;
    std::uint32_t _pad1 = 0;
};

struct FinalizePushConstants {
    std::uint32_t batch_count = 0;
    std::uint32_t _pad0 = 0;
    std::uint32_t _pad1 = 0;
    std::uint32_t _pad2 = 0;
};

void writeCullDescriptors(VkDevice device, VkDescriptorSet set, const VkDescriptorBufferInfo* input,
                          const VkDescriptorBufferInfo* output, const VkDescriptorBufferInfo* headers) {
    VkWriteDescriptorSet writes[3]{};
    const VkDescriptorBufferInfo* infos[] = {input, output, headers};
    for (std::uint32_t i = 0; i < 3; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = infos[i];
    }
    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
}

void writeFinalizeDescriptors(VkDevice device, VkDescriptorSet set, const VkDescriptorBufferInfo* headers,
                              const VkDescriptorBufferInfo* indirect) {
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = headers;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = indirect;
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
}

} // namespace

MeshBatchDrawer::MeshBatchDrawer(const VulkanContext& context) : context_(context) {
    const VkDeviceSize instance_buffer_size =
        static_cast<VkDeviceSize>(sizeof(GpuMeshInstance)) * kMaxInstancesPerFrame;
    instance_buffer_ = std::make_unique<Buffer>(
        context, instance_buffer_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    visible_instance_buffer_ = std::make_unique<Buffer>(
        context, instance_buffer_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    const VkDeviceSize header_size =
        static_cast<VkDeviceSize>(sizeof(GpuBatchCullHeader)) * kMaxBatchesPerFrame;
    batch_header_buffer_ = std::make_unique<Buffer>(
        context, header_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    const VkDeviceSize indirect_size =
        static_cast<VkDeviceSize>(sizeof(VkDrawIndexedIndirectCommand)) * kMaxBatchesPerFrame;
    indirect_buffer_ = std::make_unique<Buffer>(
        context, indirect_size,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    createCullResources();
}

void MeshBatchDrawer::createCullResources() {
    VkDescriptorSetLayoutBinding cull_bindings[3]{};
    for (std::uint32_t i = 0; i < 3; ++i) {
        cull_bindings[i].binding = i;
        cull_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        cull_bindings[i].descriptorCount = 1;
        cull_bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo cull_layout_info{};
    cull_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    cull_layout_info.bindingCount = 3;
    cull_layout_info.pBindings = cull_bindings;
    if (vkCreateDescriptorSetLayout(context_.device(), &cull_layout_info, nullptr, &cull_set_layout_) !=
        VK_SUCCESS) {
        throw std::runtime_error("Failed to create cull descriptor set layout");
    }

    VkDescriptorSetLayoutBinding finalize_bindings[2]{};
    finalize_bindings[0].binding = 0;
    finalize_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    finalize_bindings[0].descriptorCount = 1;
    finalize_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    finalize_bindings[1].binding = 1;
    finalize_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    finalize_bindings[1].descriptorCount = 1;
    finalize_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo finalize_layout_info{};
    finalize_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    finalize_layout_info.bindingCount = 2;
    finalize_layout_info.pBindings = finalize_bindings;
    if (vkCreateDescriptorSetLayout(context_.device(), &finalize_layout_info, nullptr,
                                    &finalize_set_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create finalize descriptor set layout");
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_size.descriptorCount = 8;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 2;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (vkCreateDescriptorPool(context_.device(), &pool_info, nullptr, &cull_descriptor_pool_) !=
        VK_SUCCESS) {
        throw std::runtime_error("Failed to create cull descriptor pool");
    }

    VkDescriptorSetAllocateInfo cull_alloc{};
    cull_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    cull_alloc.descriptorPool = cull_descriptor_pool_;
    cull_alloc.descriptorSetCount = 1;
    cull_alloc.pSetLayouts = &cull_set_layout_;
    if (vkAllocateDescriptorSets(context_.device(), &cull_alloc, &cull_descriptor_set_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate cull descriptor set");
    }

    VkDescriptorSetAllocateInfo finalize_alloc{};
    finalize_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    finalize_alloc.descriptorPool = cull_descriptor_pool_;
    finalize_alloc.descriptorSetCount = 1;
    finalize_alloc.pSetLayouts = &finalize_set_layout_;
    if (vkAllocateDescriptorSets(context_.device(), &finalize_alloc, &finalize_descriptor_set_) !=
        VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate finalize descriptor set");
    }

    const std::filesystem::path shader_dir = resolveShaderDirectory();
    const std::filesystem::path cull_shader = shader_dir / "cull_instances.comp.spv";
    const std::filesystem::path finalize_shader = shader_dir / "finalize_cull.comp.spv";
    if (std::filesystem::exists(cull_shader)) {
        cull_pipeline_ = std::make_unique<ComputePipeline>(context_, cull_shader, cull_set_layout_,
                                                             sizeof(CullPushConstants));
    }
    if (std::filesystem::exists(finalize_shader)) {
        finalize_pipeline_ = std::make_unique<ComputePipeline>(
            context_, finalize_shader, finalize_set_layout_, sizeof(FinalizePushConstants));
    }
}

void MeshBatchDrawer::bindDrawInstanceBuffer(const VkDevice device,
                                             const VkDescriptorSet frame_descriptor_set) {
    Buffer& source = gpu_cull_enabled_ ? *visible_instance_buffer_ : *instance_buffer_;
    VkDescriptorBufferInfo instance_info{};
    instance_info.buffer = source.handle();
    instance_info.offset = 0;
    instance_info.range = source.size();

    VkWriteDescriptorSet instance_write{};
    instance_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    instance_write.dstSet = frame_descriptor_set;
    instance_write.dstBinding = 1;
    instance_write.descriptorCount = 1;
    instance_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    instance_write.pBufferInfo = &instance_info;
    vkUpdateDescriptorSets(device, 1, &instance_write, 0, nullptr);
}

MeshBatchDrawer::~MeshBatchDrawer() {
    prepared_batches_.clear();
    batch_headers_.clear();
    cull_pipeline_.reset();
    finalize_pipeline_.reset();
    instance_buffer_.reset();
    visible_instance_buffer_.reset();
    batch_header_buffer_.reset();
    indirect_buffer_.reset();
    if (cull_descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_.device(), cull_descriptor_pool_, nullptr);
        cull_descriptor_pool_ = VK_NULL_HANDLE;
    }
    if (cull_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_.device(), cull_set_layout_, nullptr);
        cull_set_layout_ = VK_NULL_HANDLE;
    }
    if (finalize_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_.device(), finalize_set_layout_, nullptr);
        finalize_set_layout_ = VK_NULL_HANDLE;
    }
}

void MeshBatchDrawer::resetFrame() {
    instance_write_index_ = 0;
    total_instances_ = 0;
    prepared_ = false;
    prepared_batches_.clear();
    batch_headers_.clear();
    gpu_cull_enabled_ = false;
    frustum_planes_valid_ = false;
}

void MeshBatchDrawer::bindInstanceBuffer(const VkDevice device, const VkDescriptorSet frame_descriptor_set) {
    VkDescriptorBufferInfo instance_info{};
    instance_info.buffer = instance_buffer_->handle();
    instance_info.offset = 0;
    instance_info.range = instance_buffer_->size();

    VkWriteDescriptorSet instance_write{};
    instance_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    instance_write.dstSet = frame_descriptor_set;
    instance_write.dstBinding = 1;
    instance_write.descriptorCount = 1;
    instance_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    instance_write.pBufferInfo = &instance_info;
    vkUpdateDescriptorSets(device, 1, &instance_write, 0, nullptr);
}

bool MeshBatchDrawer::prepareDraw(const std::vector<SceneMeshBatch>& batches,
                                  BindlessTextures& bindless, const MeshDrawOptions& options) {
    resetFrame();
    if (batches.empty()) {
        return false;
    }

    prepared_batches_ = batches;
    std::vector<GpuMeshInstance> all_instances;
    all_instances.reserve(kMaxInstancesPerFrame);

    for (SceneMeshBatch& batch : prepared_batches_) {
        if (!batch.mesh || batch.instances.empty()) {
            continue;
        }
        if (total_instances_ + batch.instances.size() > kMaxInstancesPerFrame ||
            batch_headers_.size() >= kMaxBatchesPerFrame) {
            continue;
        }

        batch.texture_index = bindless.registerTexture(batch.base_color_texture);
        batch.material.texture_index = batch.texture_index;

        GpuBatchCullHeader header{};
        header.instance_offset = total_instances_;
        header.instance_count = static_cast<std::uint32_t>(batch.instances.size());
        header.visible_offset = total_instances_;
        header.visible_count = 0;
        batch_headers_.push_back(header);

        all_instances.insert(all_instances.end(), batch.instances.begin(), batch.instances.end());
        total_instances_ += header.instance_count;
    }

    if (all_instances.empty()) {
        prepared_batches_.clear();
        return false;
    }

    instance_buffer_->upload(all_instances.data(),
                             static_cast<VkDeviceSize>(all_instances.size()) * sizeof(GpuMeshInstance), 0);
    visible_instance_buffer_->upload(all_instances.data(),
                                     static_cast<VkDeviceSize>(all_instances.size()) * sizeof(GpuMeshInstance),
                                     0);

    buildIndirectCommands();
    uploadBatchHeaders();
    bindless.refreshDescriptorSet();

    if (options.view_proj_col_major != nullptr) {
        glm::mat4 view_proj{};
        std::memcpy(&view_proj[0][0], options.view_proj_col_major, sizeof(float) * 16);
        const Frustum frustum = frustumFromViewProjection(view_proj);
        for (std::size_t i = 0; i < frustum.planes.size(); ++i) {
            frustum_planes_[i] =
                glm::vec4(frustum.planes[i].normal, frustum.planes[i].distance);
        }
        frustum_planes_valid_ = true;
    }

    gpu_cull_enabled_ = options.enable_gpu_cull && frustum_planes_valid_ && cull_pipeline_ != nullptr &&
                        finalize_pipeline_ != nullptr;
    prepared_ = true;
    return true;
}

void MeshBatchDrawer::uploadBatchHeaders() {
    if (batch_headers_.empty()) {
        return;
    }
    for (GpuBatchCullHeader& header : batch_headers_) {
        header.visible_count = 0;
    }
    batch_header_buffer_->upload(batch_headers_.data(),
                                static_cast<VkDeviceSize>(batch_headers_.size()) *
                                    sizeof(GpuBatchCullHeader),
                                0);
}

void MeshBatchDrawer::buildIndirectCommands() {
    std::vector<VkDrawIndexedIndirectCommand> commands;
    commands.reserve(batch_headers_.size());

    std::size_t draw_batch_index = 0;
    for (const SceneMeshBatch& batch : prepared_batches_) {
        if (!batch.mesh || batch.instances.empty()) {
            continue;
        }
        if (draw_batch_index >= batch_headers_.size()) {
            break;
        }

        VkDrawIndexedIndirectCommand cmd{};
        cmd.indexCount = batch.mesh->index_count;
        cmd.instanceCount = static_cast<std::uint32_t>(batch.instances.size());
        cmd.firstIndex = 0;
        cmd.vertexOffset = 0;
        cmd.firstInstance = 0;
        commands.push_back(cmd);
        ++draw_batch_index;
    }

    if (!commands.empty()) {
        indirect_buffer_->upload(commands.data(),
                                 static_cast<VkDeviceSize>(commands.size()) *
                                     sizeof(VkDrawIndexedIndirectCommand),
                                 0);
    }
}

void MeshBatchDrawer::dispatchGpuCull(VkCommandBuffer cmd) {
    if (!prepared_ || !gpu_cull_enabled_ || batch_headers_.empty()) {
        return;
    }

    uploadBatchHeaders();

    VkDescriptorBufferInfo input_info{instance_buffer_->handle(), 0, instance_buffer_->size()};
    VkDescriptorBufferInfo output_info{visible_instance_buffer_->handle(), 0,
                                       visible_instance_buffer_->size()};
    VkDescriptorBufferInfo header_info{batch_header_buffer_->handle(), 0, batch_header_buffer_->size()};
    writeCullDescriptors(context_.device(), cull_descriptor_set_, &input_info, &output_info, &header_info);

    CullPushConstants cull_push{};
    cull_push.batch_count = static_cast<std::uint32_t>(batch_headers_.size());
    cull_push.total_instances = total_instances_;
    for (std::size_t i = 0; i < frustum_planes_.size(); ++i) {
        cull_push.frustum_planes[i] = frustum_planes_[i];
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cull_pipeline_->handle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cull_pipeline_->layout(), 0, 1,
                            &cull_descriptor_set_, 0, nullptr);
    vkCmdPushConstants(cmd, cull_pipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(CullPushConstants), &cull_push);

    const std::uint32_t group_count = (total_instances_ + 63) / 64;
    vkCmdDispatch(cmd, group_count, 1, 1);

    VkMemoryBarrier cull_barrier{};
    cull_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    cull_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    cull_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         1, &cull_barrier, 0, nullptr, 0, nullptr);

    VkDescriptorBufferInfo indirect_info{indirect_buffer_->handle(), 0, indirect_buffer_->size()};
    writeFinalizeDescriptors(context_.device(), finalize_descriptor_set_, &header_info, &indirect_info);

    FinalizePushConstants fin_push{};
    fin_push.batch_count = static_cast<std::uint32_t>(batch_headers_.size());
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, finalize_pipeline_->handle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, finalize_pipeline_->layout(), 0, 1,
                            &finalize_descriptor_set_, 0, nullptr);
    vkCmdPushConstants(cmd, finalize_pipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(FinalizePushConstants), &fin_push);
    vkCmdDispatch(cmd, (fin_push.batch_count + 63) / 64, 1, 1);

    VkMemoryBarrier draw_barrier{};
    draw_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    draw_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    draw_barrier.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1,
                         &draw_barrier, 0, nullptr, 0, nullptr);

    if (gpu_cull_enabled_) {
        VkDescriptorBufferInfo visible_info{visible_instance_buffer_->handle(), 0,
                                            visible_instance_buffer_->size()};
        VkWriteDescriptorSet visible_write{};
        visible_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        visible_write.dstBinding = 1;
        visible_write.descriptorCount = 1;
        visible_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        visible_write.pBufferInfo = &visible_info;
        (void)visible_write;
    }
}

void MeshBatchDrawer::drawIndexedBatches(VkCommandBuffer cmd, const GraphicsPipeline& pipeline,
                                         VkDescriptorSet frame_descriptor_set,
                                         BindlessTextures& bindless, bool depth_only) {
    if (!prepared_) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());

    const Mesh* current_mesh = nullptr;
    std::size_t header_index = 0;
    std::size_t indirect_index = 0;

    for (const SceneMeshBatch& batch : prepared_batches_) {
        if (!batch.mesh || batch.instances.empty()) {
            continue;
        }
        if (header_index >= batch_headers_.size()) {
            break;
        }

        const GpuBatchCullHeader& header = batch_headers_[header_index++];

        if (current_mesh != batch.mesh) {
            current_mesh = batch.mesh;
            VkBuffer vb = batch.mesh->vertex_buffer->handle();
            VkDeviceSize vb_offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vb_offset);
            vkCmdBindIndexBuffer(cmd, batch.mesh->index_buffer->handle(), 0, VK_INDEX_TYPE_UINT32);
        }

        MeshMaterialPushConstants push = batch.material;
        push.instance_base = gpu_cull_enabled_ ? header.visible_offset : header.instance_offset;

        const VkDescriptorSet sets[] = {frame_descriptor_set, bindless.descriptorSet()};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0,
                                depth_only ? 1u : 2u, sets, 0, nullptr);
        vkCmdPushConstants(cmd, pipeline.layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(MeshMaterialPushConstants), &push);

        const VkDeviceSize indirect_offset =
            static_cast<VkDeviceSize>(indirect_index * sizeof(VkDrawIndexedIndirectCommand));
        vkCmdDrawIndexedIndirect(cmd, indirect_buffer_->handle(), indirect_offset, 1,
                                 sizeof(VkDrawIndexedIndirectCommand));
        ++indirect_index;
    }
}

void MeshBatchDrawer::drawDepthPrepass(VkCommandBuffer cmd, const GraphicsPipeline& pipeline,
                                       VkDescriptorSet frame_descriptor_set,
                                       BindlessTextures& bindless) {
    drawIndexedBatches(cmd, pipeline, frame_descriptor_set, bindless, true);
}

void MeshBatchDrawer::drawColorPass(VkCommandBuffer cmd, const GraphicsPipeline& pipeline,
                                    VkDescriptorSet frame_descriptor_set,
                                    BindlessTextures& bindless) {
    drawIndexedBatches(cmd, pipeline, frame_descriptor_set, bindless, false);
}

} // namespace genesis
