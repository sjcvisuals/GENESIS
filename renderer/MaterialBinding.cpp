#include "renderer/MaterialBinding.hpp"

#include "renderer/vulkan/VulkanContext.hpp"

#include <stdexcept>

namespace genesis {

MaterialBinding::MaterialBinding(const VulkanContext& context) : context_(context) {
    VkDescriptorSetLayoutBinding sampler_binding{};
    sampler_binding.binding = 0;
    sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_binding.descriptorCount = 1;
    sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &sampler_binding;

    if (vkCreateDescriptorSetLayout(context.device(), &layout_info, nullptr, &layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create material descriptor set layout");
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 256;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 256;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;

    if (vkCreateDescriptorPool(context.device(), &pool_info, nullptr, &pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create material descriptor pool");
    }

    default_texture_ = std::make_unique<GpuTexture>(GpuTexture::createSolid(context, 255, 255, 255, 255));
    descriptorFor(default_texture_.get());
}

MaterialBinding::~MaterialBinding() {
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_.device(), pool_, nullptr);
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_.device(), layout_, nullptr);
    }
}

VkDescriptorSet MaterialBinding::allocateSet(const GpuTexture& texture) {
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout_;

    if (vkAllocateDescriptorSets(context_.device(), &alloc_info, &set) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate material descriptor set");
    }

    VkDescriptorImageInfo image_info{};
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.imageView = texture.view();
    image_info.sampler = texture.sampler();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;

    vkUpdateDescriptorSets(context_.device(), 1, &write, 0, nullptr);
    return set;
}

VkDescriptorSet MaterialBinding::descriptorFor(const GpuTexture* texture) {
    const GpuTexture& resolved = texture ? *texture : *default_texture_;
    const std::uint64_t key = reinterpret_cast<std::uint64_t>(resolved.view());
    if (const auto it = cache_.find(key); it != cache_.end()) {
        return it->second;
    }
    const VkDescriptorSet set = allocateSet(resolved);
    cache_.emplace(key, set);
    return set;
}

} // namespace genesis
