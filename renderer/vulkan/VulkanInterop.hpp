#pragma once

#include <volk.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace genesis {

void loadVulkanInterop(VkDevice device);

#ifdef _WIN32
VkResult getMemoryWin32HandleKHR(VkDevice device, const VkMemoryGetWin32HandleInfoKHR* info, HANDLE* handle);
VkResult getSemaphoreWin32HandleKHR(VkDevice device, const VkSemaphoreGetWin32HandleInfoKHR* info, HANDLE* handle);
#endif

void getImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2* info,
                                    VkMemoryRequirements2* requirements);

// Backward-compatible alias used by existing call sites.
inline void loadVulkanWin32Interop(VkDevice device) { loadVulkanInterop(device); }

} // namespace genesis
