#pragma once

#include <volk.h>
#include <vulkan/vulkan.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdint>

namespace genesis {

class VulkanContext;

struct ExternalImageExport {
#ifdef _WIN32
    HANDLE win32_handle = nullptr;
#endif
    std::uint64_t memory_size = 0;
    bool dedicated = false;
};

struct ExternalRenderTargetConfig {
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
};

// Offscreen color + depth targets with an exportable color image for PIXERA.
class ExternalRenderTarget {
public:
    ExternalRenderTarget(const VulkanContext& context, const ExternalRenderTargetConfig& config);
    ~ExternalRenderTarget();

    ExternalRenderTarget(const ExternalRenderTarget&) = delete;
    ExternalRenderTarget& operator=(const ExternalRenderTarget&) = delete;

    void recreate(std::uint32_t width, std::uint32_t height);

    VkExtent2D extent() const { return extent_; }
    VkFormat colorFormat() const { return color_format_; }
    VkRenderPass renderPass() const { return render_pass_; }
    VkFramebuffer framebuffer() const { return framebuffer_; }
    const ExternalImageExport& colorExport() const { return color_export_; }

private:
    void createImages();
    void createRenderPass();
    void createFramebuffer();
    void destroyImages();
    void destroyFramebuffer();

    const VulkanContext& context_;
    ExternalRenderTargetConfig config_;
    VkExtent2D extent_{};
    VkFormat color_format_ = VK_FORMAT_UNDEFINED;
    VkFormat depth_format_ = VK_FORMAT_D32_SFLOAT;

    VkImage color_image_ = VK_NULL_HANDLE;
    VkDeviceMemory color_memory_ = VK_NULL_HANDLE;
    VkImageView color_view_ = VK_NULL_HANDLE;
    ExternalImageExport color_export_{};

    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
};

} // namespace genesis
