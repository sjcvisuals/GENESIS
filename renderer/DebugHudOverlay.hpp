#pragma once

#include "runtime/PerfDebug.hpp"

#include <volk.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace genesis {

class VulkanContext;
class GraphicsPipeline;
class Buffer;

// Screen-space ASCII HUD drawn into an active Vulkan render pass (Renderbridge output).
class DebugHudOverlay {
public:
    DebugHudOverlay() = default;
    ~DebugHudOverlay();

    DebugHudOverlay(const DebugHudOverlay&) = delete;
    DebugHudOverlay& operator=(const DebugHudOverlay&) = delete;

    void init(const VulkanContext& context, VkRenderPass render_pass, VkExtent2D extent);
    void resize(VkExtent2D extent);
    void destroy();

    bool ready() const { return pipeline_ != nullptr && vertex_buffer_ != nullptr; }

    // Call while a compatible render pass is active.
    void draw(VkCommandBuffer cmd, const PerfDebugSnapshot& snap);

private:
    void recreatePipeline(VkRenderPass render_pass);

    const VulkanContext* context_ = nullptr;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    std::unique_ptr<GraphicsPipeline> pipeline_;
    std::unique_ptr<Buffer> vertex_buffer_;
    std::vector<std::uint8_t> cpu_quads_;
    static constexpr std::size_t kMaxVerts = 16384;
};

} // namespace genesis
