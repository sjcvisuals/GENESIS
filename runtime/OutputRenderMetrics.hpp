#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace genesis {

struct OutputRenderMetrics {
    std::string output_id;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    double render_ms = 0.0;
    std::uint32_t draw_calls = 0;
    std::uint32_t triangles = 0;
    std::uint32_t visible_mesh_nodes = 0;
};

struct MultiViewFrameMetrics {
    double shared_scene_update_ms = 0.0;
    double total_output_render_ms = 0.0;
    double wall_frame_ms = 0.0;
    std::uint32_t output_count = 0;
    std::vector<OutputRenderMetrics> outputs;
};

} // namespace genesis
