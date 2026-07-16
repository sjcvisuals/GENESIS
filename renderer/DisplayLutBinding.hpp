#pragma once

#include "color/ColorManagement.hpp"
#include "renderer/vulkan/Texture.hpp"

#include <memory>

#include <volk.h>

namespace genesis {

class VulkanContext;

// set=2 binding=0 — 3D display LUT (Rec.709 / OCIO baked).
class DisplayLutBinding {
public:
    explicit DisplayLutBinding(const VulkanContext& context);
    ~DisplayLutBinding();

    DisplayLutBinding(const DisplayLutBinding&) = delete;
    DisplayLutBinding& operator=(const DisplayLutBinding&) = delete;

    VkDescriptorSetLayout layout() const { return layout_; }
    VkDescriptorSet descriptorSet() const { return descriptor_set_; }

    void upload(const ColorLut3D& lut);

private:
    const VulkanContext& context_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    std::unique_ptr<GpuTexture> lut_texture_;
};

} // namespace genesis
