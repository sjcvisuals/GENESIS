#include "renderer/BindlessTextures.hpp"

#include "renderer/vulkan/VulkanContext.hpp"

#include <stdexcept>

namespace genesis {

BindlessTextures::BindlessTextures(const VulkanContext& context) : context_(context) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = kMaxTextures;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(context.device(), &layout_info, nullptr, &layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create bindless texture descriptor layout");
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = kMaxTextures;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;

    if (vkCreateDescriptorPool(context.device(), &pool_info, nullptr, &pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create bindless texture descriptor pool");
    }

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout_;

    if (vkAllocateDescriptorSets(context.device(), &alloc_info, &descriptor_set_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate bindless texture descriptor set");
    }

    default_texture_ = std::make_unique<GpuTexture>(GpuTexture::createSolid(context, 255, 255, 255, 255));
    registerTexture(default_texture_.get());
    refreshDescriptorSet();
}

BindlessTextures::~BindlessTextures() {
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_.device(), pool_, nullptr);
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_.device(), layout_, nullptr);
    }
}

std::uint32_t BindlessTextures::registerTexture(const GpuTexture* texture) {
    const GpuTexture& resolved = texture ? *texture : *default_texture_;
    const std::uint64_t key = reinterpret_cast<std::uint64_t>(resolved.view());
    if (const auto it = lookup_.find(key); it != lookup_.end()) {
        return it->second;
    }

    if (textures_.size() >= kMaxTextures) {
        return 0;
    }

    const std::uint32_t index = static_cast<std::uint32_t>(textures_.size());
    textures_.push_back(&resolved);
    lookup_.emplace(key, index);
    return index;
}

void BindlessTextures::refreshDescriptorSet() {
    std::vector<VkDescriptorImageInfo> image_infos(kMaxTextures);
    for (std::uint32_t i = 0; i < kMaxTextures; ++i) {
        const GpuTexture& texture =
            (i < textures_.size() && textures_[i]) ? *textures_[i] : *default_texture_;
        image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_infos[i].imageView = texture.view();
        image_infos[i].sampler = texture.sampler();
    }

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set_;
    write.dstBinding = 0;
    write.descriptorCount = kMaxTextures;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = image_infos.data();

    vkUpdateDescriptorSets(context_.device(), 1, &write, 0, nullptr);
}

} // namespace genesis
