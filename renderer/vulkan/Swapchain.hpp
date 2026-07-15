#pragma once

#include <volk.h>
#include <vulkan/vulkan.h>

#include <vector>

namespace genesis {

class VulkanContext;

struct SwapchainConfig {
    int width = 1280;
    int height = 720;
    bool vsync = true;
};

class Swapchain {
public:
    Swapchain(const VulkanContext& context, const SwapchainConfig& config);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    void recreate(int width, int height);
    VkSwapchainKHR handle() const { return swapchain_; }
    VkFormat imageFormat() const { return image_format_; }
    VkExtent2D extent() const { return extent_; }
    VkFormat depthFormat() const { return depth_format_; }
    const std::vector<VkImage>& images() const { return images_; }
    const std::vector<VkImageView>& imageViews() const { return image_views_; }
    const std::vector<VkFramebuffer>& framebuffers() const { return framebuffers_; }
    VkRenderPass renderPass() const { return render_pass_; }

    uint32_t acquireNextImage(VkSemaphore image_available);
    void present(uint32_t image_index, VkSemaphore render_finished);

private:
    void createSwapchain(int width, int height);
    void createImageViews();
    void createRenderPass();
    void createDepthResources();
    void createFramebuffers();
    void cleanupSwapchain();
    void cleanupDepth();

    const VulkanContext& context_;
    SwapchainConfig config_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat image_format_ = VK_FORMAT_UNDEFINED;
    VkFormat depth_format_ = VK_FORMAT_D32_SFLOAT;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_image_view_ = VK_NULL_HANDLE;
};

} // namespace genesis
