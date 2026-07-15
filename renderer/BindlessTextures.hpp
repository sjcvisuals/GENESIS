#pragma once

#include "renderer/vulkan/Texture.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <volk.h>

namespace genesis {

class VulkanContext;

// Bindless texture array (set 1) for mesh drawing.
class BindlessTextures {
public:
    static constexpr std::uint32_t kMaxTextures = 256;

    explicit BindlessTextures(const VulkanContext& context);
    ~BindlessTextures();

    BindlessTextures(const BindlessTextures&) = delete;
    BindlessTextures& operator=(const BindlessTextures&) = delete;

    VkDescriptorSetLayout layout() const { return layout_; }
    VkDescriptorSet descriptorSet() const { return descriptor_set_; }
    const GpuTexture& defaultTexture() const { return *default_texture_; }

    std::uint32_t registerTexture(const GpuTexture* texture);
    void refreshDescriptorSet();

private:
    const VulkanContext& context_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    std::unique_ptr<GpuTexture> default_texture_;
    std::vector<const GpuTexture*> textures_;
    std::unordered_map<std::uint64_t, std::uint32_t> lookup_;
};

} // namespace genesis
