#pragma once

#include <volk.h>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

namespace genesis {

class VulkanContext;

class Buffer {
public:
    Buffer(const VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage,
           VkMemoryPropertyFlags properties);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    VkBuffer handle() const { return buffer_; }
    VkDeviceMemory memory() const { return memory_; }
    VkDeviceSize size() const { return size_; }

    void* map();
    void unmap();
    void upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

private:
    const VulkanContext* context_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    void* mapped_ = nullptr;
};

} // namespace genesis
