#include "platform/Timer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace genesis {

void Timer::tick() {
    // High-resolution clock — SDL_GetTicks() is only 1 ms and at uncapped FPS
    // most frames report dt=0, then jump 1–2 ms. Particles accumulate that
    // quantized dt into fixed 10 ms steps → a perfectly regular motion pulse
    // while the FPS readout freezes (never "drops").
    const uint64_t now = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    if (freq <= 0.0) {
        return;
    }

    if (last_ticks_ == 0) {
        last_ticks_ = now;
        return;
    }

    double dt = static_cast<double>(now - last_ticks_) / freq;
    // Clamp spiral-of-death after stalls / debugger breaks.
    // 100 ms (was 250) — still covers a missed vsync, without dumping ~25 particle steps.
    dt = std::clamp(dt, 0.0, 0.10);
    delta_time_ = static_cast<float>(dt);
    elapsed_ += delta_time_;
    last_ticks_ = now;

    if (delta_time_ > 1e-8f) {
        const float instant = 1.0f / delta_time_;
        // Light EMA so the overlay is readable without hiding real spikes.
        fps_ = fps_ > 1.0f ? (fps_ * 0.9f + instant * 0.1f) : instant;
    }
}

} // namespace genesis
