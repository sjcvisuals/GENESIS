#pragma once

#include "renderer/vulkan/ExternalRenderTarget.hpp"

#include <volk.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace genesis {

class VulkanContext;

struct ExternalSharedImageConfig {
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
};

// Exportable sampled image for PIXERA → GENESIS video input.
// Matches RenderBridge ExtSampleD3D pattern: DLL allocates shared memory, host writes into it.
class ExternalSharedImage {
public:
    ExternalSharedImage(const VulkanContext& context, const ExternalSharedImageConfig& config);
    ~ExternalSharedImage();

    ExternalSharedImage(const ExternalSharedImage&) = delete;
    ExternalSharedImage& operator=(const ExternalSharedImage&) = delete;

    void recreate(std::uint32_t width, std::uint32_t height, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);

    VkExtent2D extent() const { return extent_; }
    VkFormat format() const { return format_; }
    VkImage image() const { return image_; }
    VkImageView view() const { return view_; }
    VkSampler sampler() const { return sampler_; }
    const ExternalImageExport& exportInfo() const { return export_info_; }

    // Device → host readback (RGBA8). Used by CPU/ORT matting path.
    bool downloadRgba8(std::vector<std::uint8_t>& out_rgba) const;

    // Host → device upload into this image (RGBA8).
    bool uploadRgba8(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height);

private:
    void destroy();
    void create();

    const VulkanContext& context_;
    ExternalSharedImageConfig config_;
    VkExtent2D extent_{};
    VkFormat format_ = VK_FORMAT_R8G8B8A8_UNORM;

    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    ExternalImageExport export_info_{};
};

} // namespace genesis
