#include "animation/AnimationEvaluator.hpp"

#include <algorithm>
#include <cmath>

namespace genesis {

namespace {

float wrapTime(float time, float duration, bool loop) {
    if (duration <= 1e-5f) {
        return 0.0f;
    }
    if (loop) {
        time = std::fmod(time, duration);
        if (time < 0.0f) {
            time += duration;
        }
        return time;
    }
    return std::clamp(time, 0.0f, duration);
}

glm::vec3 sampleVec3(const std::vector<AnimationVec3Key>& keys, float time) {
    if (keys.empty()) {
        return glm::vec3(0.0f);
    }
    if (keys.size() == 1 || time <= keys.front().time_seconds) {
        return keys.front().value;
    }
    if (time >= keys.back().time_seconds) {
        return keys.back().value;
    }
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const AnimationVec3Key& a = keys[i];
        const AnimationVec3Key& b = keys[i + 1];
        if (time >= a.time_seconds && time <= b.time_seconds) {
            const float span = std::max(b.time_seconds - a.time_seconds, 1e-5f);
            const float t = (time - a.time_seconds) / span;
            return glm::mix(a.value, b.value, t);
        }
    }
    return keys.back().value;
}

glm::quat sampleQuat(const std::vector<AnimationQuatKey>& keys, float time) {
    if (keys.empty()) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    if (keys.size() == 1 || time <= keys.front().time_seconds) {
        return glm::normalize(keys.front().value);
    }
    if (time >= keys.back().time_seconds) {
        return glm::normalize(keys.back().value);
    }
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const AnimationQuatKey& a = keys[i];
        const AnimationQuatKey& b = keys[i + 1];
        if (time >= a.time_seconds && time <= b.time_seconds) {
            const float span = std::max(b.time_seconds - a.time_seconds, 1e-5f);
            const float t = (time - a.time_seconds) / span;
            return glm::normalize(glm::slerp(a.value, b.value, t));
        }
    }
    return glm::normalize(keys.back().value);
}

Node* resolveChannelTarget(Scene& scene, const AnimationChannel& channel) {
    if (channel.target_node_id != 0) {
        if (Node* by_id = scene.find(channel.target_node_id)) {
            return by_id;
        }
    }
    if (channel.target_node_name.empty()) {
        return nullptr;
    }
    for (Node& node : scene.nodes) {
        if (node.name == channel.target_node_name) {
            return &node;
        }
    }
    return nullptr;
}

} // namespace

void applyAnimationClip(Scene& scene, const AnimationClip& clip, float time_seconds, bool loop) {
    const float t = wrapTime(time_seconds, clip.duration_seconds, loop);
    for (const AnimationChannel& channel : clip.channels) {
        Node* node = resolveChannelTarget(scene, channel);
        if (!node) {
            continue;
        }
        if (!channel.translations.empty()) {
            node->local.position = sampleVec3(channel.translations, t);
        }
        if (!channel.rotations.empty()) {
            node->local.rotation = sampleQuat(channel.rotations, t);
        }
        if (!channel.scales.empty()) {
            node->local.scale = sampleVec3(channel.scales, t);
        }
    }
}

void tickAnimationPlayback(AnimationPlayback& playback, const AnimationClip* clip, float dt_seconds) {
    if (!playback.playing || clip == nullptr || playback.clip_index < 0) {
        return;
    }
    playback.time_seconds += dt_seconds;
    if (clip->duration_seconds > 1e-5f) {
        if (playback.loop) {
            playback.time_seconds = wrapTime(playback.time_seconds, clip->duration_seconds, true);
        } else if (playback.time_seconds >= clip->duration_seconds) {
            playback.time_seconds = clip->duration_seconds;
            playback.playing = false;
        }
    }
}

} // namespace genesis
