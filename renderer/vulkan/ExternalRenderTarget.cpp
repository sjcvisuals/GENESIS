#include "renderer/vulkan/ExternalRenderTarget.hpp"

#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/VulkanInterop.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace genesis {

namespace {

VkFormat chooseDepthFormat(VkPhysicalDevice physical_device, VkFormat preferred) {
    const VkFormat candidates[] = {preferred, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                   VK_FORMAT_D24_UNORM_S8_UINT};
    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }
    throw std::runtime_error("No supported depth format found");
}

VkDeviceMemory allocateExportableImageMemory(VkDevice device, VkPhysicalDevice physical_device, VkImage image,
                                             ExternalImageExport& out_export) {
    VkMemoryDedicatedRequirements dedicated_reqs{};
    dedicated_reqs.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;

    VkImageMemoryRequirementsInfo2 req_info{};
    req_info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    req_info.image = image;

    VkMemoryRequirements2 mem_reqs{};
    mem_reqs.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    mem_reqs.pNext = &dedicated_reqs;

    getImageMemoryRequirements2KHR(device, &req_info, &mem_reqs);

    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    std::uint32_t type_index = UINT32_MAX;
    const VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    for (std::uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((mem_reqs.memoryRequirements.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & required) == required) {
            type_index = i;
            break;
        }
    }
    if (type_index == UINT32_MAX) {
        throw std::runtime_error("Failed to find device-local memory type for external image");
    }

    VkExportMemoryAllocateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = &export_info;
    alloc_info.allocationSize = mem_reqs.memoryRequirements.size;
    alloc_info.memoryTypeIndex = type_index;

    if (dedicated_reqs.requiresDedicatedAllocation) {
        VkMemoryDedicatedAllocateInfo dedicated_info{};
        dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated_info.image = image;
        dedicated_info.pNext = &export_info;
        alloc_info.pNext = &dedicated_info;
        out_export.dedicated = true;
    } else {
        alloc_info.pNext = &export_info;
        out_export.dedicated = false;
    }

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &alloc_info, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate external image memory");
    }

    out_export.memory_size = mem_reqs.memoryRequirements.size;

#ifdef _WIN32
    VkMemoryGetWin32HandleInfoKHR handle_info{};
    handle_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    handle_info.memory = memory;
    handle_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;

    HANDLE handle = nullptr;
    if (getMemoryWin32HandleKHR(device, &handle_info, &handle) != VK_SUCCESS) {
        vkFreeMemory(device, memory, nullptr);
        throw std::runtime_error("Failed to export external image Win32 handle");
    }
    out_export.win32_handle = handle;
#endif

    return memory;
}

VkDeviceMemory allocateImageMemory(VkDevice device, VkPhysicalDevice physical_device, VkImage image,
                                   VkMemoryPropertyFlags properties) {
    VkMemoryRequirements mem_reqs{};
    vkGetImageMemoryRequirements(device, image, &mem_reqs);

    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    std::uint32_t type_index = UINT32_MAX;
    for (std::uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((mem_reqs.memoryTypeBits & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            type_index = i;
            break;
        }
    }
    if (type_index == UINT32_MAX) {
        throw std::runtime_error("Failed to find suitable memory type");
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = type_index;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &alloc_info, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image memory");
    }
    return memory;
}

VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange.aspectMask = aspect;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device, &info, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view");
    }
    return view;
}

} // namespace

ExternalRenderTarget::ExternalRenderTarget(const VulkanContext& context, const ExternalRenderTargetConfig& config)
    : context_(context), config_(config) {
    extent_ = {config.width, config.height};
    color_format_ = config.color_format;
    depth_format_ = chooseDepthFormat(context.physicalDevice(), config.depth_format);
    createRenderPass();
    createImages();
    createFramebuffer();
}

ExternalRenderTarget::~ExternalRenderTarget() {
    if (context_.device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.device());
    }
    destroyFramebuffer();
    destroyImages();
    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(context_.device(), render_pass_, nullptr);
    }
}

void ExternalRenderTarget::recreate(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    vkDeviceWaitIdle(context_.device());
    destroyFramebuffer();
    destroyImages();
    config_.width = width;
    config_.height = height;
    extent_ = {width, height};
    createImages();
    createFramebuffer();
}

void ExternalRenderTarget::createImages() {
    VkExternalMemoryImageCreateInfo external_info{};
    external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;

    VkImageCreateInfo color_info{};
    color_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    color_info.pNext = &external_info;
    color_info.imageType = VK_IMAGE_TYPE_2D;
    color_info.format = color_format_;
    color_info.extent = {extent_.width, extent_.height, 1};
    color_info.mipLevels = 1;
    color_info.arrayLayers = 1;
    color_info.samples = VK_SAMPLE_COUNT_1_BIT;
    color_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    color_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    color_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(context_.device(), &color_info, nullptr, &color_image_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create external color image");
    }

    color_memory_ =
        allocateExportableImageMemory(context_.device(), context_.physicalDevice(), color_image_, color_export_);
    if (vkBindImageMemory(context_.device(), color_image_, color_memory_, 0) != VK_SUCCESS) {
        throw std::runtime_error("Failed to bind external color image memory");
    }
    color_view_ = createImageView(context_.device(), color_image_, color_format_, VK_IMAGE_ASPECT_COLOR_BIT);

    VkImageCreateInfo depth_info{};
    depth_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depth_info.imageType = VK_IMAGE_TYPE_2D;
    depth_info.format = depth_format_;
    depth_info.extent = {extent_.width, extent_.height, 1};
    depth_info.mipLevels = 1;
    depth_info.arrayLayers = 1;
    depth_info.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    depth_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depth_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(context_.device(), &depth_info, nullptr, &depth_image_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image");
    }

    depth_memory_ = allocateImageMemory(context_.device(), context_.physicalDevice(), depth_image_,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkBindImageMemory(context_.device(), depth_image_, depth_memory_, 0) != VK_SUCCESS) {
        throw std::runtime_error("Failed to bind depth image memory");
    }
    depth_view_ = createImageView(context_.device(), depth_image_, depth_format_, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void ExternalRenderTarget::destroyImages() {
    VkDevice device = context_.device();
    if (color_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, color_view_, nullptr);
        color_view_ = VK_NULL_HANDLE;
    }
    if (depth_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depth_view_, nullptr);
        depth_view_ = VK_NULL_HANDLE;
    }
    if (color_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, color_image_, nullptr);
        color_image_ = VK_NULL_HANDLE;
    }
    if (depth_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, depth_image_, nullptr);
        depth_image_ = VK_NULL_HANDLE;
    }
    if (color_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, color_memory_, nullptr);
        color_memory_ = VK_NULL_HANDLE;
    }
    if (depth_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, depth_memory_, nullptr);
        depth_memory_ = VK_NULL_HANDLE;
    }
    color_export_ = {};
}

void ExternalRenderTarget::createRenderPass() {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = color_format_;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth_attachment{};
    depth_attachment.format = depth_format_;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {color_attachment, depth_attachment};

    VkRenderPassCreateInfo pass_info{};
    pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    pass_info.pAttachments = attachments.data();
    pass_info.subpassCount = 1;
    pass_info.pSubpasses = &subpass;
    pass_info.dependencyCount = 1;
    pass_info.pDependencies = &dependency;

    if (vkCreateRenderPass(context_.device(), &pass_info, nullptr, &render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create external render pass");
    }
}

void ExternalRenderTarget::createFramebuffer() {
    std::array<VkImageView, 2> attachments = {color_view_, depth_view_};

    VkFramebufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = render_pass_;
    info.attachmentCount = static_cast<uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.width = extent_.width;
    info.height = extent_.height;
    info.layers = 1;

    if (vkCreateFramebuffer(context_.device(), &info, nullptr, &framebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create external framebuffer");
    }
}

void ExternalRenderTarget::destroyFramebuffer() {
    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(context_.device(), framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }
}

} // namespace genesis
