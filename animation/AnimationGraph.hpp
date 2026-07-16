#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace genesis {

struct AnimationGraphState {
    std::string name;
    std::int32_t clip_index = -1;
    bool loop = true;
};

struct AnimationGraphTransition {
    std::string from_state;
    std::string to_state;
    // Reserved for conditions / crossfade duration.
    float blend_seconds = 0.15f;
};

// Lightweight state machine that selects which clip the timeline/player drives.
struct AnimationGraph {
    std::string entry_state;
    std::string active_state;
    std::vector<AnimationGraphState> states;
    std::vector<AnimationGraphTransition> transitions;

    void ensureDefaults();
    bool setActiveState(const std::string& state_name);
    const AnimationGraphState* activeState() const;
    AnimationGraphState* findState(const std::string& name);
    const AnimationGraphState* findState(const std::string& name) const;
};

} // namespace genesis
