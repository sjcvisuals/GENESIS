#pragma once

#include <cstdint>
#include <vector>

#include <volk.h>

namespace genesis {

class VulkanContext;

class GpuTexture {
public:
    GpuTexture() = default;
    GpuTexture(const VulkanContext& context, const std::uint8_t* rgba, std::uint32_t width,
               std::uint32_t height);
    ~GpuTexture();

    GpuTexture(GpuTexture&& other) noexcept;
    GpuTexture& operator=(GpuTexture&& other) noexcept;

    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;

    VkImageView view() const { return view_; }
    VkSampler sampler() const { return sampler_; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }

    static GpuTexture createSolid(const VulkanContext& context, std::uint8_t r, std::uint8_t g,
                                  std::uint8_t b, std::uint8_t a = 255);

private:
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    std::uint32_t width_ = 1;
    std::uint32_t height_ = 1;
};

} // namespace genesis
