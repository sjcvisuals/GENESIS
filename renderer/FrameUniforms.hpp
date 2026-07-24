#pragma once

#include <cstdint>

namespace genesis {

// std140 layout — must match shaders/mesh.vert, mesh.frag, grid.vert
struct GpuLight {
    float position[4];
    float direction[4];
    float color[4];
    float params[4]; // type, range, inner_cos, outer_cos
};

struct FrameUniforms {
    float view_proj[16];
    float camera_pos[4]; // xyz = eye, w = exposure
    std::int32_t light_count;
    float ibl_intensity;
    float fog_density;
    float fog_height;
    GpuLight lights[8];
    float ambient_sky[4];
    float ambient_ground[4];
    float fog_color[4];
    float display_params[4]; // x=mode, y=bloom, z=vignette, w=time(+grain via settings)
};

static_assert(sizeof(GpuLight) == 64, "GpuLight std140 size mismatch");
static_assert(sizeof(FrameUniforms) == 672, "FrameUniforms std140 size mismatch");

} // namespace genesis
