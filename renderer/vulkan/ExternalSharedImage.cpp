#include "renderer/vulkan/ExternalSharedImage.hpp"

#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/VulkanInterop.hpp"

#include <cstring>
#include <stdexcept>

namespace genesis {

namespace {

VkDeviceMemory allocateExportableSampledMemory(VkDevice device, VkPhysicalDevice physical_device, VkImage image,
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
        throw std::runtime_error("ExternalSharedImage: no device-local memory type");
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.memoryRequirements.size;
    alloc_info.memoryTypeIndex = type_index;

#ifdef _WIN32
    VkExportMemoryAllocateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;

    VkMemoryDedicatedAllocateInfo dedicated_info{};
    if (dedicated_reqs.requiresDedicatedAllocation) {
        dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated_info.image = image;
        dedicated_info.pNext = &export_info;
        alloc_info.pNext = &dedicated_info;
        out_export.dedicated = true;
    } else {
        alloc_info.pNext = &export_info;
        out_export.dedicated = false;
    }
#else
    (void)dedicated_reqs;
    out_export.dedicated = false;
#endif

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &alloc_info, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("ExternalSharedImage: allocate failed");
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
        throw std::runtime_error("ExternalSharedImage: Win32 export failed");
    }
    out_export.win32_handle = handle;
#endif
    return memory;
}

} // namespace

ExternalSharedImage::ExternalSharedImage(const VulkanContext& context,
                                         const ExternalSharedImageConfig& config)
    : context_(context), config_(config) {
    create();
}

ExternalSharedImage::~ExternalSharedImage() {
    destroy();
}

void ExternalSharedImage::recreate(std::uint32_t width, std::uint32_t height, VkFormat format) {
    config_.width = width;
    config_.height = height;
    config_.format = format;
    destroy();
    create();
}

void ExternalSharedImage::destroy() {
#ifdef _WIN32
    if (export_info_.win32_handle) {
        CloseHandle(export_info_.win32_handle);
        export_info_.win32_handle = nullptr;
    }
#endif
    export_info_ = {};
    if (sampler_) {
        vkDestroySampler(context_.device(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (view_) {
        vkDestroyImageView(context_.device(), view_, nullptr);
        view_ = VK_NULL_HANDLE;
    }
    if (image_) {
        vkDestroyImage(context_.device(), image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (memory_) {
        vkFreeMemory(context_.device(), memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
}

void ExternalSharedImage::create() {
    extent_ = {config_.width, config_.height};
    format_ = config_.format;
    if (extent_.width == 0 || extent_.height == 0) {
        throw std::runtime_error("ExternalSharedImage: invalid extent");
    }

    VkExternalMemoryImageCreateInfo external_info{};
    external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
#ifdef _WIN32
    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
#else
    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;
#endif

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = &external_info;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format_;
    image_info.extent = {extent_.width, extent_.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(context_.device(), &image_info, nullptr, &image_) != VK_SUCCESS) {
        throw std::runtime_error("ExternalSharedImage: vkCreateImage failed");
    }

    memory_ = allocateExportableSampledMemory(context_.device(), context_.physicalDevice(), image_,
                                              export_info_);
    if (vkBindImageMemory(context_.device(), image_, memory_, 0) != VK_SUCCESS) {
        throw std::runtime_error("ExternalSharedImage: bind failed");
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format_;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(context_.device(), &view_info, nullptr, &view_) != VK_SUCCESS) {
        throw std::runtime_error("ExternalSharedImage: view failed");
    }

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(context_.device(), &sampler_info, nullptr, &sampler_) != VK_SUCCESS) {
        throw std::runtime_error("ExternalSharedImage: sampler failed");
    }
}

bool ExternalSharedImage::downloadRgba8(std::vector<std::uint8_t>& out_rgba) const {
    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(extent_.width) * extent_.height * 4ull;
    out_rgba.resize(static_cast<std::size_t>(bytes));

    VkBufferCreateInfo buf_info{};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = bytes;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    if (vkCreateBuffer(context_.device(), &buf_info, nullptr, &staging) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements reqs{};
    vkGetBufferMemoryRequirements(context_.device(), staging, &reqs);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = reqs.size;
    alloc.memoryTypeIndex = context_.findMemoryType(
        reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (vkAllocateMemory(context_.device(), &alloc, nullptr, &staging_mem) != VK_SUCCESS) {
        vkDestroyBuffer(context_.device(), staging, nullptr);
        return false;
    }
    vkBindBufferMemory(context_.device(), staging, staging_mem, 0);

    context_.immediateSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier to_src{};
        to_src.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_src.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_src.image = image_;
        to_src.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_src.subresourceRange.levelCount = 1;
        to_src.subresourceRange.layerCount = 1;
        to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &to_src);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {extent_.width, extent_.height, 1};
        vkCmdCopyImageToBuffer(cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);
    });

    void* mapped = nullptr;
    vkMapMemory(context_.device(), staging_mem, 0, bytes, 0, &mapped);
    std::memcpy(out_rgba.data(), mapped, static_cast<std::size_t>(bytes));
    vkUnmapMemory(context_.device(), staging_mem);

    vkDestroyBuffer(context_.device(), staging, nullptr);
    vkFreeMemory(context_.device(), staging_mem, nullptr);
    return true;
}

bool ExternalSharedImage::uploadRgba8(const std::uint8_t* rgba, std::uint32_t width,
                                      std::uint32_t height) {
    if (!rgba || width != extent_.width || height != extent_.height) {
        return false;
    }
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4ull;

    VkBufferCreateInfo buf_info{};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = bytes;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    if (vkCreateBuffer(context_.device(), &buf_info, nullptr, &staging) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements reqs{};
    vkGetBufferMemoryRequirements(context_.device(), staging, &reqs);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = reqs.size;
    alloc.memoryTypeIndex = context_.findMemoryType(
        reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (vkAllocateMemory(context_.device(), &alloc, nullptr, &staging_mem) != VK_SUCCESS) {
        vkDestroyBuffer(context_.device(), staging, nullptr);
        return false;
    }
    vkBindBufferMemory(context_.device(), staging, staging_mem, 0);

    void* mapped = nullptr;
    vkMapMemory(context_.device(), staging_mem, 0, bytes, 0, &mapped);
    std::memcpy(mapped, rgba, static_cast<std::size_t>(bytes));
    vkUnmapMemory(context_.device(), staging_mem);

    context_.immediateSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier to_dst{};
        to_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_dst.image = image_;
        to_dst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_dst.subresourceRange.levelCount = 1;
        to_dst.subresourceRange.layerCount = 1;
        to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &to_dst);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    });

    vkDestroyBuffer(context_.device(), staging, nullptr);
    vkFreeMemory(context_.device(), staging_mem, nullptr);
    return true;
}

} // namespace genesis
