#include "renderer/vulkan/VulkanInterop.hpp"

namespace genesis {

namespace {

#ifdef _WIN32
PFN_vkGetMemoryWin32HandleKHR pfnGetMemoryWin32HandleKHR = nullptr;
PFN_vkGetSemaphoreWin32HandleKHR pfnGetSemaphoreWin32HandleKHR = nullptr;
#endif
PFN_vkGetImageMemoryRequirements2 pfnGetImageMemoryRequirements2KHR = nullptr;

} // namespace

void loadVulkanInterop(VkDevice device) {
#ifdef _WIN32
    pfnGetMemoryWin32HandleKHR = reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(
        vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandleKHR"));
    pfnGetSemaphoreWin32HandleKHR = reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(
        vkGetDeviceProcAddr(device, "vkGetSemaphoreWin32HandleKHR"));
#endif
    pfnGetImageMemoryRequirements2KHR = reinterpret_cast<PFN_vkGetImageMemoryRequirements2>(
        vkGetDeviceProcAddr(device, "vkGetImageMemoryRequirements2"));
}

#ifdef _WIN32
VkResult getMemoryWin32HandleKHR(VkDevice device, const VkMemoryGetWin32HandleInfoKHR* info, HANDLE* handle) {
    return pfnGetMemoryWin32HandleKHR ? pfnGetMemoryWin32HandleKHR(device, info, handle)
                                      : VK_ERROR_EXTENSION_NOT_PRESENT;
}

VkResult getSemaphoreWin32HandleKHR(VkDevice device, const VkSemaphoreGetWin32HandleInfoKHR* info,
                                    HANDLE* handle) {
    return pfnGetSemaphoreWin32HandleKHR ? pfnGetSemaphoreWin32HandleKHR(device, info, handle)
                                         : VK_ERROR_EXTENSION_NOT_PRESENT;
}
#endif

void getImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2* info,
                                    VkMemoryRequirements2* requirements) {
    if (pfnGetImageMemoryRequirements2KHR) {
        pfnGetImageMemoryRequirements2KHR(device, info, requirements);
        return;
    }
    // Core Vulkan 1.1 fallback when the KHR entry point is unavailable.
    if (vkGetImageMemoryRequirements2) {
        vkGetImageMemoryRequirements2(device, info, requirements);
        return;
    }
    VkMemoryRequirements legacy{};
    vkGetImageMemoryRequirements(device, info->image, &legacy);
    requirements->memoryRequirements = legacy;
}

} // namespace genesis
