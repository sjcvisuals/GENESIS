#pragma once

#include <cstdint>

namespace genesis {

class Timer {
public:
    void tick();
    float deltaTime() const { return delta_time_; }
    float elapsed() const { return elapsed_; }
    float fps() const { return fps_; }

private:
    uint64_t last_ticks_ = 0;
    float delta_time_ = 0.0f;
    float elapsed_ = 0.0f;
    float fps_ = 0.0f;
};

} // namespace genesis
