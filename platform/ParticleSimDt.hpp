#pragma once

#include <algorithm>
#include <cmath>

namespace genesis {

// Convert noisy wall-clock frame dt into a particle sim dt that stays smooth.
//
// Particles advance on a fixed 10 ms accumulator. Raw frame dt that oscillates
// around the FPS-cap period (e.g. 18↔22 ms @ 50 FPS) flips the step count
// (1↔3) and reads as a regular beat hitch. When the FPS cap is on, snap near-
// target frames to the exact period so step counts stay stable (2 @ 50 FPS).
// Always hard-clamp after stalls so catch-up cannot dump a huge jump.
inline float particleSimDt(float wall_dt_seconds, bool fps_cap_enabled, float fps_cap) {
    constexpr float kMaxDtSeconds = 0.05f; // 50 ms — ~2.5 frames @ 50 FPS
    float dt = std::clamp(wall_dt_seconds, 0.0f, kMaxDtSeconds);

    if (!fps_cap_enabled || fps_cap < 1.0f) {
        return dt;
    }

    const float period = 1.0f / fps_cap;
    // Within 35% of the paced period → treat as exact (eliminates 1↔3 flicker).
    if (std::abs(dt - period) <= period * 0.35f) {
        return period;
    }
    // Real hitch / missed frames: allow limited catch-up (at most 2 periods).
    return std::min(dt, period * 2.0f);
}

} // namespace genesis
