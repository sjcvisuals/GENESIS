#include "renderer/vulkan/Texture.hpp"

#include "renderer/vulkan/Buffer.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

#include <stdexcept>

namespace genesis {

namespace {

VkSampler createDefaultSampler(VkDevice device) {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.maxAnisotropy = 1.0f;
    info.minLod = 0.0f;
    info.maxLod = 0.0f;

    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler");
    }
    return sampler;
}

} // namespace

GpuTexture::GpuTexture(const VulkanContext& context, const std::uint8_t* rgba,
                       const std::uint32_t width, const std::uint32_t height)
    : device_(context.device()), width_(width), height_(height) {
    if (width == 0 || height == 0 || !rgba) {
        throw std::runtime_error("Invalid texture upload dimensions");
    }

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device_, &image_info, nullptr, &image_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GPU texture image");
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, image_, &requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex =
        context.findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device_, &alloc_info, nullptr, &memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate GPU texture memory");
    }
    vkBindImageMemory(device_, image_, memory_, 0);

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &view_info, nullptr, &view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GPU texture view");
    }

    sampler_ = createDefaultSampler(device_);

    const VkDeviceSize image_size = static_cast<VkDeviceSize>(width) * height * 4;
    Buffer staging(context, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.upload(rgba, image_size);

    context.immediateSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier to_transfer{};
        to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_transfer.srcAccessMask = 0;
        to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_transfer.image = image_;
        to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_transfer.subresourceRange.levelCount = 1;
        to_transfer.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &to_transfer);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(cmd, staging.handle(), image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);

        VkImageMemoryBarrier to_shader{};
        to_shader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_shader.image = image_;
        to_shader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_shader.subresourceRange.levelCount = 1;
        to_shader.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &to_shader);
    });
}

GpuTexture GpuTexture::createSolid(const VulkanContext& context, const std::uint8_t r,
                                   const std::uint8_t g, const std::uint8_t b, const std::uint8_t a) {
    const std::uint8_t pixel[4] = {r, g, b, a};
    return GpuTexture(context, pixel, 1, 1);
}

GpuTexture::GpuTexture(GpuTexture&& other) noexcept {
    *this = std::move(other);
}

GpuTexture& GpuTexture::operator=(GpuTexture&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        image_ = other.image_;
        memory_ = other.memory_;
        view_ = other.view_;
        sampler_ = other.sampler_;
        width_ = other.width_;
        height_ = other.height_;
        other.device_ = VK_NULL_HANDLE;
        other.image_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.view_ = VK_NULL_HANDLE;
        other.sampler_ = VK_NULL_HANDLE;
    }
    return *this;
}

GpuTexture::~GpuTexture() {
    destroy();
}

void GpuTexture::destroy() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, view_, nullptr);
        view_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

} // namespace genesis
