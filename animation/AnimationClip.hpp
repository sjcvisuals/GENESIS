#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace genesis {

struct AnimationVec3Key {
    float time_seconds = 0.0f;
    glm::vec3 value{0.0f};
};

struct AnimationQuatKey {
    float time_seconds = 0.0f;
    glm::quat value{1.0f, 0.0f, 0.0f, 0.0f};
};

enum class AnimationTrack : std::uint8_t {
    Translation = 1,
    Rotation = 2,
    Scale = 4,
    Transform = 7, // Translation | Rotation | Scale
};

inline AnimationTrack operator|(AnimationTrack a, AnimationTrack b) {
    return static_cast<AnimationTrack>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
inline bool hasTrack(AnimationTrack mask, AnimationTrack bit) {
    return (static_cast<std::uint8_t>(mask) & static_cast<std::uint8_t>(bit)) != 0;
}

// Targets a scene node by id (preferred) and/or name/path (import + document load).
struct AnimationChannel {
    std::uint64_t target_node_id = 0;
    std::string target_node_name;
    std::vector<AnimationVec3Key> translations;
    std::vector<AnimationQuatKey> rotations;
    std::vector<AnimationVec3Key> scales;
};

struct AnimationClip {
    std::string name;
    float duration_seconds = 0.0f;
    float ticks_per_second = 25.0f;
    std::vector<AnimationChannel> channels;
};

struct AnimationPlayback {
    bool playing = false;
    bool loop = true;
    float time_seconds = 0.0f;
    std::int32_t clip_index = -1; // index into SceneRuntime clips; -1 = none
};

} // namespace genesis
