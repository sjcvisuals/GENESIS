#include "renderer/vulkan/Swapchain.hpp"

#include "renderer/vulkan/VulkanContext.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace genesis {

namespace {

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    if (formats.empty()) {
        throw std::runtime_error("No Vulkan surface formats available");
    }
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats[0];
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync) {
    if (!vsync) {
        for (const auto mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }
        for (const auto mode : modes) {
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                return mode;
            }
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, int width, int height) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D extent{
        static_cast<uint32_t>(std::clamp(width, static_cast<int>(capabilities.minImageExtent.width),
                                         static_cast<int>(capabilities.maxImageExtent.width))),
        static_cast<uint32_t>(std::clamp(height, static_cast<int>(capabilities.minImageExtent.height),
                                          static_cast<int>(capabilities.maxImageExtent.height)))};
    return extent;
}

VkFormat chooseDepthFormat(VkPhysicalDevice physical_device) {
    const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }
    throw std::runtime_error("No supported depth format found");
}

} // namespace

Swapchain::Swapchain(const VulkanContext& context, const SwapchainConfig& config)
    : context_(context), config_(config) {
    createSwapchain(config.width, config.height);
    createImageViews();
    createRenderPass();
    createDepthResources();
    createFramebuffers();
}

Swapchain::~Swapchain() {
    cleanupSwapchain();
    cleanupDepth();
    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(context_.device(), render_pass_, nullptr);
    }
}

void Swapchain::recreate(int width, int height) {
    vkDeviceWaitIdle(context_.device());
    cleanupSwapchain();
    cleanupDepth();
    createSwapchain(width, height);
    createImageViews();
    createDepthResources();
    createFramebuffers();
}

void Swapchain::createSwapchain(int width, int height) {
    VkPhysicalDevice physical_device = context_.physicalDevice();
    VkSurfaceKHR surface = context_.surface();

    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats.data());

    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, nullptr);
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count,
                                              present_modes.data());

    const VkSurfaceFormatKHR surface_format = chooseSurfaceFormat(formats);
    const VkPresentModeKHR present_mode = choosePresentMode(present_modes, config_.vsync);
    extent_ = chooseExtent(capabilities, width, height);
    image_format_ = surface_format.format;

    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(capabilities.supportedCompositeAlpha & composite_alpha)) {
        if (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
            composite_alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        } else if (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
            composite_alpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        } else if (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) {
            composite_alpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        }
    }

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface;
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent_;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = composite_alpha;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = swapchain_;

    const uint32_t queue_families[] = {context_.graphicsQueueFamily(), context_.presentQueueFamily()};
    if (context_.graphicsQueueFamily() != context_.presentQueueFamily()) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_families;
    }

    if (vkCreateSwapchainKHR(context_.device(), &create_info, nullptr, &swapchain_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain");
    }

    if (create_info.oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context_.device(), create_info.oldSwapchain, nullptr);
    }

    uint32_t swapchain_image_count = 0;
    vkGetSwapchainImagesKHR(context_.device(), swapchain_, &swapchain_image_count, nullptr);
    images_.resize(swapchain_image_count);
    vkGetSwapchainImagesKHR(context_.device(), swapchain_, &swapchain_image_count, images_.data());
}

void Swapchain::createImageViews() {
    image_views_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = images_[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = image_format_;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(context_.device(), &view_info, nullptr, &image_views_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swapchain image view");
        }
    }
}

void Swapchain::createRenderPass() {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = image_format_;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth_attachment{};
    depth_attachment.format = depth_format_;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[2] = {color_attachment, depth_attachment};
    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 2;
    render_pass_info.pAttachments = attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    if (vkCreateRenderPass(context_.device(), &render_pass_info, nullptr, &render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }
}

void Swapchain::createDepthResources() {
    depth_format_ = chooseDepthFormat(context_.physicalDevice());

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = extent_.width;
    image_info.extent.height = extent_.height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = depth_format_;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(context_.device(), &image_info, nullptr, &depth_image_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image");
    }

    VkMemoryRequirements mem_req{};
    vkGetImageMemoryRequirements(context_.device(), depth_image_, &mem_req);

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = mem_req.size;
    alloc.memoryTypeIndex = context_.findMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(context_.device(), &alloc, nullptr, &depth_memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate depth memory");
    }
    vkBindImageMemory(context_.device(), depth_image_, depth_memory_, 0);

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = depth_image_;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = depth_format_;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view.subresourceRange.baseMipLevel = 0;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.baseArrayLayer = 0;
    view.subresourceRange.layerCount = 1;

    if (vkCreateImageView(context_.device(), &view, nullptr, &depth_image_view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image view");
    }
}

void Swapchain::createFramebuffers() {
    framebuffers_.resize(image_views_.size());
    for (size_t i = 0; i < image_views_.size(); ++i) {
        VkImageView attachments[] = {image_views_[i], depth_image_view_};
        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass_;
        framebuffer_info.attachmentCount = 2;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = extent_.width;
        framebuffer_info.height = extent_.height;
        framebuffer_info.layers = 1;

        if (vkCreateFramebuffer(context_.device(), &framebuffer_info, nullptr, &framebuffers_[i]) !=
            VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer");
        }
    }
}

void Swapchain::cleanupSwapchain() {
    for (auto framebuffer : framebuffers_) {
        vkDestroyFramebuffer(context_.device(), framebuffer, nullptr);
    }
    framebuffers_.clear();

    for (auto view : image_views_) {
        vkDestroyImageView(context_.device(), view, nullptr);
    }
    image_views_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context_.device(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    images_.clear();
}

void Swapchain::cleanupDepth() {
    if (depth_image_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(context_.device(), depth_image_view_, nullptr);
        depth_image_view_ = VK_NULL_HANDLE;
    }
    if (depth_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(context_.device(), depth_image_, nullptr);
        depth_image_ = VK_NULL_HANDLE;
    }
    if (depth_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(context_.device(), depth_memory_, nullptr);
        depth_memory_ = VK_NULL_HANDLE;
    }
}

uint32_t Swapchain::acquireNextImage(VkSemaphore image_available) {
    uint32_t image_index = 0;
    const VkResult result =
        vkAcquireNextImageKHR(context_.device(), swapchain_, UINT64_MAX, image_available, VK_NULL_HANDLE,
                              &image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate(static_cast<int>(extent_.width), static_cast<int>(extent_.height));
        return acquireNextImage(image_available);
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swapchain image");
    }
    return image_index;
}

void Swapchain::present(uint32_t image_index, VkSemaphore render_finished) {
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_;
    present_info.pImageIndices = &image_index;

    const VkResult result = vkQueuePresentKHR(context_.presentQueue(), &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate(static_cast<int>(extent_.width), static_cast<int>(extent_.height));
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image");
    }
}

} // namespace genesis
