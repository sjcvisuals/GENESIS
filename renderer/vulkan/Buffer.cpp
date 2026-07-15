#include "renderer/vulkan/Buffer.hpp"

#include "renderer/vulkan/VulkanContext.hpp"

#include <cstring>
#include <stdexcept>

namespace genesis {

Buffer::Buffer(const VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage,
               VkMemoryPropertyFlags properties)
    : context_(&context), size_(size) {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(context.device(), &buffer_info, nullptr, &buffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(context.device(), buffer_, &requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = context.findMemoryType(requirements.memoryTypeBits, properties);

    if (vkAllocateMemory(context.device(), &alloc_info, nullptr, &memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory");
    }

    vkBindBufferMemory(context.device(), buffer_, memory_, 0);
}

Buffer::~Buffer() {
    if (context_ == nullptr) {
        return;
    }
    if (mapped_) {
        unmap();
    }
    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(context_->device(), buffer_, nullptr);
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(context_->device(), memory_, nullptr);
    }
}

Buffer::Buffer(Buffer&& other) noexcept
    : context_(other.context_), buffer_(other.buffer_), memory_(other.memory_), size_(other.size_),
      mapped_(other.mapped_) {
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.mapped_ = nullptr;
    other.context_ = nullptr;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        this->~Buffer();
        context_ = other.context_;
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        size_ = other.size_;
        mapped_ = other.mapped_;
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.mapped_ = nullptr;
        other.context_ = nullptr;
    }
    return *this;
}

void* Buffer::map() {
    if (!mapped_) {
        if (vkMapMemory(context_->device(), memory_, 0, size_, 0, &mapped_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to map buffer memory");
        }
    }
    return mapped_;
}

void Buffer::unmap() {
    if (mapped_) {
        vkUnmapMemory(context_->device(), memory_);
        mapped_ = nullptr;
    }
}

void Buffer::upload(const void* data, VkDeviceSize size, VkDeviceSize offset) {
    void* dst = map();
    std::memcpy(static_cast<char*>(dst) + offset, data, static_cast<size_t>(size));
}

} // namespace genesis
