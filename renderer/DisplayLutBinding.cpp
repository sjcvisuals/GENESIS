#include "renderer/DisplayLutBinding.hpp"

#include "renderer/vulkan/VulkanContext.hpp"

#include <stdexcept>

namespace genesis {

DisplayLutBinding::DisplayLutBinding(const VulkanContext& context) : context_(context) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(context.device(), &layout_info, nullptr, &layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create display LUT descriptor layout");
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (vkCreateDescriptorPool(context.device(), &pool_info, nullptr, &pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create display LUT descriptor pool");
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layout_;
    if (vkAllocateDescriptorSets(context.device(), &alloc, &descriptor_set_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate display LUT descriptor set");
    }

    ColorLut3D identity{};
    identity.size = ColorLut3D::kSize;
    identity.rgba.assign(static_cast<std::size_t>(identity.size) * identity.size * identity.size * 4, 0.0f);
    for (int z = 0; z < identity.size; ++z) {
        for (int y = 0; y < identity.size; ++y) {
            for (int x = 0; x < identity.size; ++x) {
                const std::size_t idx =
                    (static_cast<std::size_t>(z) * identity.size * identity.size +
                     static_cast<std::size_t>(y) * identity.size + static_cast<std::size_t>(x)) *
                    4;
                identity.rgba[idx + 0] = static_cast<float>(x) / static_cast<float>(identity.size - 1);
                identity.rgba[idx + 1] = static_cast<float>(y) / static_cast<float>(identity.size - 1);
                identity.rgba[idx + 2] = static_cast<float>(z) / static_cast<float>(identity.size - 1);
                identity.rgba[idx + 3] = 1.0f;
            }
        }
    }
    upload(identity);
}

DisplayLutBinding::~DisplayLutBinding() {
    lut_texture_.reset();
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_.device(), pool_, nullptr);
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_.device(), layout_, nullptr);
    }
}

void DisplayLutBinding::upload(const ColorLut3D& lut) {
    if (context_.device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.device());
    }
    lut_texture_ = std::make_unique<GpuTexture>(
        GpuTexture::createLut3D(context_, lut.rgba.data(), lut.size));

    VkDescriptorImageInfo image_info{};
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.imageView = lut_texture_->view();
    image_info.sampler = lut_texture_->sampler();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(context_.device(), 1, &write, 0, nullptr);
}

} // namespace genesis
