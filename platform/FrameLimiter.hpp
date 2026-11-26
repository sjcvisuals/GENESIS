#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>

namespace genesis {

// Wall-clock frame pacing for editor / player. Default 50 FPS keeps particle
// motion and OSC tracking steady on variable-refresh Windows machines.
//
// Particles in the editor advance on a fixed 10 ms step accumulator driven by
// wall-clock dt. Coarse SDL_Delay-only waits (±1–15 ms) make frame dt oscillate
// (e.g. 18 vs 22 ms → 1 vs 3 sim steps) and look like a regular beat hitch.
// Sleep most of the wait, then spin the last ~2 ms against an absolute deadline.
class FrameLimiter {
public:
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    void setTargetFps(float fps) { target_fps_ = std::clamp(fps, 1.0f, 240.0f); }
    float targetFps() const { return target_fps_; }

    void beginFrame() { frame_start_ = SDL_GetPerformanceCounter(); }

    // Sleep so (work + wait) ≈ 1/target_fps. Call after presenting the frame.
    void endFrame() {
        if (!enabled_ || target_fps_ < 1.0f || frame_start_ == 0) {
            return;
        }
        const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
        if (freq <= 0.0) {
            return;
        }
        const double target = 1.0 / static_cast<double>(target_fps_);
        // Absolute deadline from frame start — avoids drift when Delay overshoots.
        const double deadline =
            static_cast<double>(frame_start_) / freq + target;

        auto seconds_now = [&]() -> double {
            return static_cast<double>(SDL_GetPerformanceCounter()) / freq;
        };

        // Sleep all but the last 2 ms — Delay granularity is ~1 ms and often overshoots.
        // Larger reserve than before: Windows timer coalescing made 1.5 ms too tight and
        // left us overshooting into the next fixed particle step.
        constexpr double kSpinReserve = 0.002;
        const double remain = deadline - seconds_now();
        if (remain > kSpinReserve) {
            const double sleep_s = remain - kSpinReserve;
            SDL_DelayNS(static_cast<Uint64>(sleep_s * 1'000'000'000.0));
        }
        while (seconds_now() < deadline) {
            // Busy-wait the remainder for stable particle step counts.
        }
    }

private:
    bool enabled_ = true;
    float target_fps_ = 50.0f;
    uint64_t frame_start_ = 0;
};

} // namespace genesis
