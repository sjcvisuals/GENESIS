#pragma once

#include <cstddef>
#include <cstdint>

namespace genesis {

// Lightweight stage diagnostics for PIXERA Remover (no Vulkan dependency).
struct RemoverFrameDebug {
    const char* stage = "none"; // waiting_video_in / download_fail / passthrough / matte / error
    bool has_video_in = false;
    bool download_ok = false;
    bool matte_copied = false;
    std::size_t nonzero_rgb = 0;
    unsigned mid_r = 0;
    unsigned mid_g = 0;
    unsigned mid_b = 0;
    unsigned mid_a = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

} // namespace genesis
