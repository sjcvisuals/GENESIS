#pragma once

#include <cstdint>

namespace genesis {

struct FrameStats {
    float frame_ms = 0.0f;
    float update_ms = 0.0f;
    float imgui_ms = 0.0f;
    float draw_ms = 0.0f;

    std::uint32_t draw_calls = 0;
    std::uint32_t triangles = 0;
    std::uint32_t visible_mesh_nodes = 0;
    std::uint32_t scene_nodes = 0;
    std::uint32_t active_lights = 0;
};

} // namespace genesis
