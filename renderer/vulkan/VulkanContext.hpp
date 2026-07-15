#pragma once

#include <volk.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct SDL_Window;

namespace genesis {

struct VulkanContextConfig {
    SDL_Window* window = nullptr;
    // When window is null, selects the GPU by LUID (0 = first graphics-capable device).
    std::uint64_t adapter_luid = 0;
    bool enable_validation = true;
    std::vector<const char*> extra_instance_extensions;
    std::vector<const char*> extra_device_extensions;
};

bool configIsHeadless(const VulkanContextConfig& config);

class VulkanContext {
public:
    explicit VulkanContext(const VulkanContextConfig& config);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physicalDevice() const { return physical_device_; }
    VkDevice device() const { return device_; }
    VkQueue graphicsQueue() const { return graphics_queue_; }
    VkQueue presentQueue() const { return present_queue_; }
    uint32_t graphicsQueueFamily() const { return graphics_queue_family_; }
    uint32_t presentQueueFamily() const { return present_queue_family_; }
    VkSurfaceKHR surface() const { return surface_; }
    bool isHeadless() const { return headless_; }
    const std::vector<const char*>& deviceExtensions() const { return device_extensions_; }

    uint32_t findMemoryType(uint32_t type_bits, VkMemoryPropertyFlags properties) const;

    // Records + submits a one-time command buffer on the graphics queue, then waits.
    // Intended for infrequent resource uploads (staging copies), not per-frame use.
    void immediateSubmit(const std::function<void(VkCommandBuffer)>& record) const;

private:
    void createInstance(const VulkanContextConfig& config);
    void setupDebugMessenger();
    void createSurface(SDL_Window* window);
    void pickPhysicalDevice(std::uint64_t adapter_luid);
    void createLogicalDevice(const VulkanContextConfig& config);

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    uint32_t graphics_queue_family_ = 0;
    uint32_t present_queue_family_ = 0;
    bool validation_enabled_ = false;
    bool headless_ = false;
    std::vector<const char*> device_extensions_;
};

} // namespace genesis
