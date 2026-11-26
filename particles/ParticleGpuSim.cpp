#include "particles/ParticleGpuSim.hpp"

#include "platform/Paths.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace genesis {

namespace {

constexpr std::uint32_t kMinCapacity = 256;

} // namespace

ParticleGpuSim::ParticleGpuSim(const VulkanContext& context) : context_(context) {
    createPipeline();
}

ParticleGpuSim::~ParticleGpuSim() {
    destroyImmediateResources();
    pipeline_.reset();
    buffer_.reset();
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_.device(), pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
    if (set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_.device(), set_layout_, nullptr);
        set_layout_ = VK_NULL_HANDLE;
    }
}

void ParticleGpuSim::ensureImmediateResources() {
    if (immediate_pool_ != VK_NULL_HANDLE && immediate_cmd_ != VK_NULL_HANDLE &&
        immediate_fence_ != VK_NULL_HANDLE) {
        return;
    }
    destroyImmediateResources();

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = context_.graphicsQueueFamily();
    if (vkCreateCommandPool(context_.device(), &pool_info, nullptr, &immediate_pool_) !=
        VK_SUCCESS) {
        throw std::runtime_error("Failed to create particle GPU immediate command pool");
    }

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = immediate_pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(context_.device(), &alloc, &immediate_cmd_) != VK_SUCCESS) {
        destroyImmediateResources();
        throw std::runtime_error("Failed to allocate particle GPU immediate command buffer");
    }

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(context_.device(), &fence_info, nullptr, &immediate_fence_) != VK_SUCCESS) {
        destroyImmediateResources();
        throw std::runtime_error("Failed to create particle GPU immediate fence");
    }
}

void ParticleGpuSim::destroyImmediateResources() {
    if (immediate_fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(context_.device(), immediate_fence_, nullptr);
        immediate_fence_ = VK_NULL_HANDLE;
    }
    // Command buffers are freed with the pool.
    immediate_cmd_ = VK_NULL_HANDLE;
    if (immediate_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_.device(), immediate_pool_, nullptr);
        immediate_pool_ = VK_NULL_HANDLE;
    }
}

void ParticleGpuSim::flushImmediateAndWait(std::uint32_t count, std::uint32_t steps, float dt,
                                           float sim_time_seconds) {
    if (!ready() || count == 0 || steps == 0) {
        return;
    }
    ensureImmediateResources();

    if (vkWaitForFences(context_.device(), 1, &immediate_fence_, VK_TRUE, UINT64_MAX) !=
        VK_SUCCESS) {
        throw std::runtime_error("Failed waiting on particle GPU immediate fence");
    }
    if (vkResetFences(context_.device(), 1, &immediate_fence_) != VK_SUCCESS) {
        throw std::runtime_error("Failed resetting particle GPU immediate fence");
    }
    if (vkResetCommandBuffer(immediate_cmd_, 0) != VK_SUCCESS) {
        throw std::runtime_error("Failed resetting particle GPU immediate command buffer");
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(immediate_cmd_, &begin) != VK_SUCCESS) {
        throw std::runtime_error("Failed beginning particle GPU immediate command buffer");
    }
    dispatchUpdates(immediate_cmd_, count, steps, dt, sim_time_seconds);
    if (vkEndCommandBuffer(immediate_cmd_) != VK_SUCCESS) {
        throw std::runtime_error("Failed ending particle GPU immediate command buffer");
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &immediate_cmd_;
    if (vkQueueSubmit(context_.graphicsQueue(), 1, &submit, immediate_fence_) != VK_SUCCESS) {
        throw std::runtime_error("Failed submitting particle GPU immediate compute");
    }
    if (vkWaitForFences(context_.device(), 1, &immediate_fence_, VK_TRUE, UINT64_MAX) !=
        VK_SUCCESS) {
        throw std::runtime_error("Failed waiting for particle GPU compute");
    }
}

void ParticleGpuSim::createPipeline() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(context_.device(), &layout_info, nullptr, &set_layout_) !=
        VK_SUCCESS) {
        throw std::runtime_error("Failed to create particle GPU descriptor set layout");
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_size.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (vkCreateDescriptorPool(context_.device(), &pool_info, nullptr, &pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create particle GPU descriptor pool");
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &set_layout_;
    if (vkAllocateDescriptorSets(context_.device(), &alloc, &set_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate particle GPU descriptor set");
    }

    const std::filesystem::path shader = resolveShaderDirectory() / "particle_update.comp.spv";
    if (!std::filesystem::exists(shader)) {
        return; // leave pipeline_ null; caller falls back to CPU
    }
    pipeline_ = std::make_unique<ComputePipeline>(context_, shader, set_layout_, sizeof(PushConstants));
    ensureCapacity(kMinCapacity);
}

void ParticleGpuSim::ensureCapacity(std::uint32_t count) {
    const std::uint32_t need = std::max(count, kMinCapacity);
    if (buffer_ && capacity_ >= need) {
        return;
    }
    capacity_ = need;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(sizeof(GpuParticle)) * capacity_;
    buffer_ = std::make_unique<Buffer>(
        context_, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    writeDescriptors();
}

void ParticleGpuSim::writeDescriptors() {
    if (!buffer_ || set_ == VK_NULL_HANDLE) {
        return;
    }
    VkDescriptorBufferInfo info{};
    info.buffer = buffer_->handle();
    info.offset = 0;
    info.range = buffer_->size();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(context_.device(), 1, &write, 0, nullptr);
}

void ParticleGpuSim::upload(const GpuParticle* particles, std::uint32_t count) {
    if (count == 0) {
        return;
    }
    ensureCapacity(count);
    buffer_->upload(particles, static_cast<VkDeviceSize>(sizeof(GpuParticle)) * count);
}

void ParticleGpuSim::dispatchUpdates(VkCommandBuffer cmd, std::uint32_t count, std::uint32_t steps,
                                     float dt, float sim_time_seconds) {
    if (!ready() || count == 0 || steps == 0 || cmd == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_->handle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_->layout(), 0, 1, &set_, 0,
                            nullptr);

    const std::uint32_t groups = (count + 63u) / 64u;
    float time = sim_time_seconds;
    for (std::uint32_t s = 0; s < steps; ++s) {
        PushConstants push{};
        push.count = count;
        push.dt = dt;
        push.sim_time = time;
        vkCmdPushConstants(cmd, pipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push),
                           &push);
        vkCmdDispatch(cmd, groups, 1, 1);

        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                                VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                             &barrier, 0, nullptr, 0, nullptr);
        time += dt;
    }
}

void ParticleGpuSim::download(GpuParticle* particles, std::uint32_t count) {
    if (!buffer_ || count == 0) {
        return;
    }
    void* mapped = buffer_->map();
    std::memcpy(particles, mapped, sizeof(GpuParticle) * count);
    buffer_->unmap();
}

} // namespace genesis
