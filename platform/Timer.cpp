#include "platform/Timer.hpp"

#include <SDL3/SDL.h>

namespace genesis {

void Timer::tick() {
    const uint64_t now = SDL_GetTicks();
    if (last_ticks_ == 0) {
        last_ticks_ = now;
        return;
    }

    delta_time_ = static_cast<float>(now - last_ticks_) / 1000.0f;
    elapsed_ += delta_time_;
    last_ticks_ = now;

    if (delta_time_ > 0.0f) {
        fps_ = 1.0f / delta_time_;
    }
}

} // namespace genesis
