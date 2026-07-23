#include "renderer/vulkan/ExternalSync.hpp"

#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/VulkanInterop.hpp"

#include <stdexcept>

namespace genesis {

namespace {

VkSemaphore createExportableSemaphore(VkDevice device) {
    VkSemaphoreCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

#ifdef _WIN32
    VkExportSemaphoreCreateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
    create_info.pNext = &export_info;
#endif

    VkSemaphore semaphore = VK_NULL_HANDLE;
    if (vkCreateSemaphore(device, &create_info, nullptr, &semaphore) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create exportable semaphore");
    }
    return semaphore;
}

#ifdef _WIN32
HANDLE exportSemaphoreHandle(VkDevice device, VkSemaphore semaphore) {
    VkSemaphoreGetWin32HandleInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
    info.semaphore = semaphore;
    info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;

    HANDLE handle = nullptr;
    if (getSemaphoreWin32HandleKHR(device, &info, &handle) != VK_SUCCESS) {
        throw std::runtime_error("Failed to export semaphore Win32 handle");
    }
    return handle;
}
#endif

} // namespace

ExternalSync::ExternalSync(const VulkanContext& context) : device_(context.device()) {
    host_finished_ = createExportableSemaphore(device_);
    render_finished_ = createExportableSemaphore(device_);
#ifdef _WIN32
    host_finished_handle_ = exportSemaphoreHandle(device_, host_finished_);
    render_finished_handle_ = exportSemaphoreHandle(device_, render_finished_);
#endif
}

ExternalSync::~ExternalSync() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    if (host_finished_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, host_finished_, nullptr);
    }
    if (render_finished_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, render_finished_, nullptr);
    }
}

} // namespace genesis
