#include "animation/AnimationGraph.hpp"

namespace genesis {

void AnimationGraph::ensureDefaults() {
    if (states.empty()) {
        AnimationGraphState idle{};
        idle.name = "Idle";
        idle.clip_index = -1;
        idle.loop = true;
        states.push_back(idle);
        entry_state = "Idle";
        active_state = "Idle";
        return;
    }
    if (entry_state.empty()) {
        entry_state = states.front().name;
    }
    if (active_state.empty() || findState(active_state) == nullptr) {
        active_state = entry_state;
    }
}

AnimationGraphState* AnimationGraph::findState(const std::string& name) {
    for (AnimationGraphState& state : states) {
        if (state.name == name) {
            return &state;
        }
    }
    return nullptr;
}

const AnimationGraphState* AnimationGraph::findState(const std::string& name) const {
    for (const AnimationGraphState& state : states) {
        if (state.name == name) {
            return &state;
        }
    }
    return nullptr;
}

bool AnimationGraph::setActiveState(const std::string& state_name) {
    if (findState(state_name) == nullptr) {
        return false;
    }
    active_state = state_name;
    return true;
}

const AnimationGraphState* AnimationGraph::activeState() const {
    return findState(active_state);
}

} // namespace genesis
