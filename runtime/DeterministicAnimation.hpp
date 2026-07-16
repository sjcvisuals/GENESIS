#pragma once

#include "runtime/GenesisFrameTime.hpp"
#include "scene/Scene.hpp"

#include <glm/vec3.hpp>

#include <string>

namespace genesis {

struct DeterministicAnimationConfig {
    std::string node_path;
    glm::vec3 rotation_axis{0.0f, 1.0f, 0.0f};
    float rotation_radians_per_second = 1.0f;
    glm::vec3 position_base{0.0f, 0.0f, 0.0f};
    float position_amplitude = 0.0f;
    glm::vec3 position_axis{0.0f, 1.0f, 0.0f};
};

// Applies a deterministic test animation driven by production time (not wall clock).
void applyDeterministicAnimation(Scene& scene, const GenesisFrameTime& frame_time,
                                 const DeterministicAnimationConfig& config = {});

} // namespace genesis
