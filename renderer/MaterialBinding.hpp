#pragma once

#include "renderer/vulkan/Texture.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <volk.h>

namespace genesis {

class VulkanContext;

// Manages material texture descriptor sets (set 1) for mesh drawing.
class MaterialBinding {
public:
    explicit MaterialBinding(const VulkanContext& context);
    ~MaterialBinding();

    MaterialBinding(const MaterialBinding&) = delete;
    MaterialBinding& operator=(const MaterialBinding&) = delete;

    VkDescriptorSetLayout layout() const { return layout_; }
    const GpuTexture& defaultTexture() const { return *default_texture_; }
    VkDescriptorSet descriptorFor(const GpuTexture* texture);

private:
    VkDescriptorSet allocateSet(const GpuTexture& texture);

    const VulkanContext& context_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::unique_ptr<GpuTexture> default_texture_;
    std::unordered_map<std::uint64_t, VkDescriptorSet> cache_;
};

} // namespace genesis
