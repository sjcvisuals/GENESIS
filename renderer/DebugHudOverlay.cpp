#include "renderer/DebugHudOverlay.hpp"

#include "platform/Paths.hpp"
#include "renderer/vulkan/Buffer.hpp"
#include "renderer/vulkan/Pipeline.hpp"
#include "renderer/vulkan/VulkanContext.hpp"

#include <stb/stb_easy_font.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace genesis {

namespace {

struct HudVertex {
    float x = 0.0f;
    float y = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct HudPushConstants {
    float screen_size[2]{};
    float scale = 2.5f;
    float pad = 0.0f;
};

} // namespace

DebugHudOverlay::~DebugHudOverlay() {
    destroy();
}

void DebugHudOverlay::destroy() {
    pipeline_.reset();
    vertex_buffer_.reset();
    context_ = nullptr;
    render_pass_ = VK_NULL_HANDLE;
}

void DebugHudOverlay::init(const VulkanContext& context, VkRenderPass render_pass, VkExtent2D extent) {
    destroy();
    context_ = &context;
    render_pass_ = render_pass;
    extent_ = extent;

    const VkDeviceSize vbytes = sizeof(HudVertex) * kMaxVerts;
    vertex_buffer_ = std::make_unique<Buffer>(
        context, vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    cpu_quads_.resize(99999);
    recreatePipeline(render_pass);
}

void DebugHudOverlay::resize(VkExtent2D extent) {
    extent_ = extent;
    if (context_ && render_pass_ != VK_NULL_HANDLE) {
        recreatePipeline(render_pass_);
    }
}

void DebugHudOverlay::recreatePipeline(VkRenderPass render_pass) {
    pipeline_.reset();
    if (!context_ || render_pass == VK_NULL_HANDLE) {
        return;
    }
    render_pass_ = render_pass;

    const std::filesystem::path shader_dir = resolveShaderDirectory();
    GraphicsPipelineConfig cfg{};
    cfg.render_pass = render_pass;
    cfg.vert_shader = shader_dir / "debug_hud.vert.spv";
    cfg.frag_shader = shader_dir / "debug_hud.frag.spv";
    cfg.extent = extent_;
    cfg.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    cfg.depth_test = false;
    cfg.depth_write = false;
    cfg.alpha_blend = true;
    cfg.cull_mode = VK_CULL_MODE_NONE;
    cfg.vertex_stride = sizeof(HudVertex);
    cfg.vertex_attributes = {
        {0, VK_FORMAT_R32G32_SFLOAT, 0},
        {1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 2)},
    };
    cfg.push_constant_size = sizeof(HudPushConstants);
    cfg.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT;

    if (!std::filesystem::exists(cfg.vert_shader) || !std::filesystem::exists(cfg.frag_shader)) {
        spdlog::warn("DebugHudOverlay: missing shaders in {}", shader_dir.string());
        return;
    }
    pipeline_ = std::make_unique<GraphicsPipeline>(*context_, cfg);
}

void DebugHudOverlay::draw(VkCommandBuffer cmd, const PerfDebugSnapshot& snap) {
    if (!ready() || cmd == VK_NULL_HANDLE || extent_.width == 0 || extent_.height == 0) {
        return;
    }

    char text[768]{};
    formatPerfDebugText(text, sizeof(text), snap);

    // Semi-transparent panel background as two triangles (pixel space before scale).
    const float panel_w = static_cast<float>(stb_easy_font_width(text)) + 16.0f;
    const float panel_h = static_cast<float>(stb_easy_font_height(text)) + 14.0f;
    const float ox = 10.0f;
    const float oy = 10.0f;

    std::vector<HudVertex> verts;
    verts.reserve(512);

    auto push_tri = [&](float x0, float y0, float x1, float y1, float x2, float y2, float r, float g,
                        float b, float a) {
        verts.push_back({x0, y0, r, g, b, a});
        verts.push_back({x1, y1, r, g, b, a});
        verts.push_back({x2, y2, r, g, b, a});
    };
    push_tri(ox, oy, ox + panel_w, oy, ox + panel_w, oy + panel_h, 0.02f, 0.03f, 0.04f, 0.72f);
    push_tri(ox, oy, ox + panel_w, oy + panel_h, ox, oy + panel_h, 0.02f, 0.03f, 0.04f, 0.72f);

    unsigned char color[4] = {210, 230, 240, 255};
    const int quads =
        stb_easy_font_print(ox + 6.0f, oy + 5.0f, text, color, cpu_quads_.data(),
                            static_cast<int>(cpu_quads_.size()));
    for (int q = 0; q < quads; ++q) {
        const char* base = reinterpret_cast<const char*>(cpu_quads_.data()) + q * 64;
        struct EasyV {
            float x, y, z;
            unsigned char c[4];
        };
        const EasyV* v = reinterpret_cast<const EasyV*>(base);
        // Quad verts 0-1-2-3 → triangles 0-1-2 and 0-2-3
        auto emit = [&](int i) {
            verts.push_back({v[i].x, v[i].y, v[i].c[0] / 255.0f, v[i].c[1] / 255.0f,
                             v[i].c[2] / 255.0f, v[i].c[3] / 255.0f});
        };
        emit(0);
        emit(1);
        emit(2);
        emit(0);
        emit(2);
        emit(3);
    }

    const std::uint32_t vert_count =
        static_cast<std::uint32_t>(std::min(verts.size(), kMaxVerts));
    if (vert_count == 0) {
        return;
    }
    vertex_buffer_->upload(verts.data(), sizeof(HudVertex) * vert_count, 0);

    HudPushConstants pc{};
    pc.screen_size[0] = static_cast<float>(extent_.width);
    pc.screen_size[1] = static_cast<float>(extent_.height);
    pc.scale = 2.6f;

    VkViewport viewport{};
    viewport.width = pc.screen_size[0];
    viewport.height = pc.screen_size[1];
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->handle());
    vkCmdPushConstants(cmd, pipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    const VkBuffer vb = vertex_buffer_->handle();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
    vkCmdDraw(cmd, vert_count, 1, 0, 0);
}

} // namespace genesis
