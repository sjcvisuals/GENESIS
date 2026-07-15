#pragma once

#include <volk.h>

namespace genesis {

void loadVulkanWin32Interop(VkDevice device);

VkResult getMemoryWin32HandleKHR(VkDevice device, const VkMemoryGetWin32HandleInfoKHR* info, HANDLE* handle);
VkResult getSemaphoreWin32HandleKHR(VkDevice device, const VkSemaphoreGetWin32HandleInfoKHR* info, HANDLE* handle);
void getImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2* info,
                                    VkMemoryRequirements2* requirements);

} // namespace genesis
