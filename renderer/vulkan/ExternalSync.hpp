#pragma once

#include <volk.h>
#include <vulkan/vulkan.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace genesis {

class VulkanContext;

// Binary semaphores exported for PIXERA / RX GPU handoff (ExtSampleVK pattern).
class ExternalSync {
public:
    explicit ExternalSync(const VulkanContext& context);
    ~ExternalSync();

    ExternalSync(const ExternalSync&) = delete;
    ExternalSync& operator=(const ExternalSync&) = delete;

    VkSemaphore hostFinishedSemaphore() const { return host_finished_; }
    VkSemaphore renderFinishedSemaphore() const { return render_finished_; }

#ifdef _WIN32
    HANDLE hostFinishedHandle() const { return host_finished_handle_; }
    HANDLE renderFinishedHandle() const { return render_finished_handle_; }
#endif

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkSemaphore host_finished_ = VK_NULL_HANDLE;
    VkSemaphore render_finished_ = VK_NULL_HANDLE;
#ifdef _WIN32
    HANDLE host_finished_handle_ = nullptr;
    HANDLE render_finished_handle_ = nullptr;
#endif
};

} // namespace genesis
