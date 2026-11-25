#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>

namespace genesis {

// Wall-clock frame pacing for editor / player. Default 50 FPS keeps particle
// motion and OSC tracking steady on variable-refresh Windows machines.
class FrameLimiter {
public:
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    void setTargetFps(float fps) { target_fps_ = std::clamp(fps, 1.0f, 240.0f); }
    float targetFps() const { return target_fps_; }

    void beginFrame() { frame_start_ = SDL_GetPerformanceCounter(); }

    // Sleep so (work + wait) ≈ 1/target_fps. Call after presenting the frame.
    void endFrame() {
        if (!enabled_ || target_fps_ < 1.0f) {
            return;
        }
        const uint64_t now = SDL_GetPerformanceCounter();
        const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
        if (freq <= 0.0 || frame_start_ == 0) {
            return;
        }
        const double elapsed = static_cast<double>(now - frame_start_) / freq;
        const double target = 1.0 / static_cast<double>(target_fps_);
        const double remain = target - elapsed;
        if (remain >= 0.001) {
            SDL_Delay(static_cast<Uint32>(remain * 1000.0));
        }
    }

private:
    bool enabled_ = true;
    float target_fps_ = 50.0f;
    uint64_t frame_start_ = 0;
};

} // namespace genesis
