#pragma once

#include <volk.h>
#include <vulkan/vulkan.h>

struct SDL_Window;

namespace genesis {

class VulkanContext;
class Swapchain;

class ImGuiLayer {
public:
    ImGuiLayer(SDL_Window* window, const VulkanContext& context, Swapchain& swapchain);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void beginFrame();
    void endFrame(VkCommandBuffer command_buffer);
    void onResize();

    // Safe for process lifetime; uses EditorPaths static storage.
    void enableLayoutPersistence();

private:
    void createDescriptorPool(const VulkanContext& context);

    SDL_Window* window_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
};

} // namespace genesis
