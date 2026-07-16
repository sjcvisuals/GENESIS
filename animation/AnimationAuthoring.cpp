#include "animation/AnimationAuthoring.hpp"

#include "scene/ScenePath.hpp"

#include <algorithm>
#include <cmath>

namespace genesis {

namespace {

constexpr float kKeyTimeEpsilon = 1e-4f;

template <typename KeyT>
std::size_t lowerBoundTime(const std::vector<KeyT>& keys, float time) {
    return static_cast<std::size_t>(std::distance(
        keys.begin(),
        std::lower_bound(keys.begin(), keys.end(), time,
                         [](const KeyT& key, float t) { return key.time_seconds < t; })));
}

} // namespace

void upsertVec3Key(std::vector<AnimationVec3Key>& keys, float time_seconds, const glm::vec3& value) {
    const std::size_t i = lowerBoundTime(keys, time_seconds);
    if (i < keys.size() && std::fabs(keys[i].time_seconds - time_seconds) <= kKeyTimeEpsilon) {
        keys[i].value = value;
        keys[i].time_seconds = time_seconds;
        return;
    }
    if (i > 0 && std::fabs(keys[i - 1].time_seconds - time_seconds) <= kKeyTimeEpsilon) {
        keys[i - 1].value = value;
        keys[i - 1].time_seconds = time_seconds;
        return;
    }
    keys.insert(keys.begin() + static_cast<std::ptrdiff_t>(i),
                AnimationVec3Key{time_seconds, value});
}

void upsertQuatKey(std::vector<AnimationQuatKey>& keys, float time_seconds, const glm::quat& value) {
    const glm::quat q = glm::normalize(value);
    const std::size_t i = lowerBoundTime(keys, time_seconds);
    if (i < keys.size() && std::fabs(keys[i].time_seconds - time_seconds) <= kKeyTimeEpsilon) {
        keys[i].value = q;
        keys[i].time_seconds = time_seconds;
        return;
    }
    if (i > 0 && std::fabs(keys[i - 1].time_seconds - time_seconds) <= kKeyTimeEpsilon) {
        keys[i - 1].value = q;
        keys[i - 1].time_seconds = time_seconds;
        return;
    }
    keys.insert(keys.begin() + static_cast<std::ptrdiff_t>(i), AnimationQuatKey{time_seconds, q});
}

bool removeVec3KeyAt(std::vector<AnimationVec3Key>& keys, float time_seconds) {
    for (auto it = keys.begin(); it != keys.end(); ++it) {
        if (std::fabs(it->time_seconds - time_seconds) <= kKeyTimeEpsilon) {
            keys.erase(it);
            return true;
        }
    }
    return false;
}

bool removeQuatKeyAt(std::vector<AnimationQuatKey>& keys, float time_seconds) {
    for (auto it = keys.begin(); it != keys.end(); ++it) {
        if (std::fabs(it->time_seconds - time_seconds) <= kKeyTimeEpsilon) {
            keys.erase(it);
            return true;
        }
    }
    return false;
}

AnimationChannel* findOrCreateChannel(AnimationClip& clip, NodeId node_id, const std::string& node_name) {
    for (AnimationChannel& channel : clip.channels) {
        if (node_id != 0 && channel.target_node_id == node_id) {
            channel.target_node_name = node_name;
            return &channel;
        }
        if (!node_name.empty() && channel.target_node_name == node_name) {
            channel.target_node_id = node_id;
            return &channel;
        }
    }
    AnimationChannel& channel = clip.channels.emplace_back();
    channel.target_node_id = node_id;
    channel.target_node_name = node_name;
    return &channel;
}

void keyframeNode(AnimationClip& clip, const Node& node, float time_seconds, AnimationTrack tracks) {
    AnimationChannel* channel = findOrCreateChannel(clip, node.id, node.name);
    if (hasTrack(tracks, AnimationTrack::Translation)) {
        upsertVec3Key(channel->translations, time_seconds, node.local.position);
    }
    if (hasTrack(tracks, AnimationTrack::Rotation)) {
        upsertQuatKey(channel->rotations, time_seconds, node.local.rotation);
    }
    if (hasTrack(tracks, AnimationTrack::Scale)) {
        upsertVec3Key(channel->scales, time_seconds, node.local.scale);
    }
    refreshClipDuration(clip);
}

void removeKeysAtTime(AnimationClip& clip, NodeId node_id, float time_seconds, AnimationTrack tracks) {
    for (AnimationChannel& channel : clip.channels) {
        if (node_id == 0 || channel.target_node_id != node_id) {
            continue;
        }
        if (hasTrack(tracks, AnimationTrack::Translation)) {
            removeVec3KeyAt(channel.translations, time_seconds);
        }
        if (hasTrack(tracks, AnimationTrack::Rotation)) {
            removeQuatKeyAt(channel.rotations, time_seconds);
        }
        if (hasTrack(tracks, AnimationTrack::Scale)) {
            removeVec3KeyAt(channel.scales, time_seconds);
        }
    }
    refreshClipDuration(clip);
}

void refreshClipDuration(AnimationClip& clip) {
    float max_t = 0.0f;
    for (const AnimationChannel& channel : clip.channels) {
        if (!channel.translations.empty()) {
            max_t = std::max(max_t, channel.translations.back().time_seconds);
        }
        if (!channel.rotations.empty()) {
            max_t = std::max(max_t, channel.rotations.back().time_seconds);
        }
        if (!channel.scales.empty()) {
            max_t = std::max(max_t, channel.scales.back().time_seconds);
        }
    }
    // Cover the last key; keep at least 1s when keys exist only at t=0 so the timeline has range.
    if (max_t <= 1e-5f) {
        const bool any_keys = std::any_of(
            clip.channels.begin(), clip.channels.end(), [](const AnimationChannel& c) {
                return !c.translations.empty() || !c.rotations.empty() || !c.scales.empty();
            });
        clip.duration_seconds = any_keys ? std::max(clip.duration_seconds, 1.0f) : clip.duration_seconds;
        return;
    }
    clip.duration_seconds = std::max(clip.duration_seconds, max_t);
}

void bindClipChannels(Scene& scene, AnimationClip& clip) {
    for (AnimationChannel& channel : clip.channels) {
        if (channel.target_node_id != 0) {
            if (Node* by_id = scene.find(channel.target_node_id)) {
                channel.target_node_name = by_id->name;
                continue;
            }
            channel.target_node_id = 0;
        }
        if (channel.target_node_name.empty()) {
            continue;
        }
        if (const std::optional<NodeId> path_id = findNodeByPath(scene, channel.target_node_name)) {
            channel.target_node_id = *path_id;
            if (Node* n = scene.find(*path_id)) {
                channel.target_node_name = n->name;
            }
            continue;
        }
        for (Node& node : scene.nodes) {
            if (node.name == channel.target_node_name) {
                channel.target_node_id = node.id;
                break;
            }
        }
    }
}

} // namespace genesis
