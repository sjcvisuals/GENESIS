#pragma once

#include <cstdint>

namespace genesis {

// std140 layout — must match shaders/mesh.vert and shaders/mesh.frag
struct GpuLight {
    float position[4];
    float direction[4];
    float color[4];
    float params[4]; // type, range, inner_cos, outer_cos
};

struct FrameUniforms {
    float view_proj[16];
    float camera_pos[4];
    std::int32_t light_count;
    std::int32_t _pad[3];
    GpuLight lights[8];
};

static_assert(sizeof(FrameUniforms) == 96 + 8 * sizeof(GpuLight), "FrameUniforms std140 size mismatch");

} // namespace genesis
