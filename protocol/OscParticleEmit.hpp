#pragma once

// Built-in OSC → particle burst mapping.
// Default address: /genesis/particles/emit
//   ,ff     x y
//   ,fff    x y count
//   ,ffff   x y z count
//
// XY is mapped onto a world plane (default XZ) so dual-GPU sessions that share
// the same OSC stream + scene_seed stay visually locked.

#include "particles/ParticleEmitter.hpp"
#include "protocol/OscTypes.hpp"

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <string>

namespace genesis {

enum class OscParticlePlane : std::uint8_t {
    XZ = 0, // x→X, y→Z, height on Y (stage / floor)
    XY = 1, // x→X, y→Y, depth on Z (screen plane)
};

struct OscParticleEmitSettings {
    bool enabled = true;
    std::string address = "/genesis/particles/emit";
    std::string emitter_path = "Scene/Particle System/OSC Tracker";
    OscParticlePlane plane = OscParticlePlane::XZ;
    float plane_height = 0.15f;
    float scale = 1.0f;
    glm::vec2 offset{0.0f, 0.0f};
    std::uint32_t default_count = 8;
    std::uint32_t max_count = 64;
    // Move the emitter node to each OSC xy (continuous spawn uses the emitter rate).
    bool track_emitter_position = true;
    // Also burst `count` particles at the point (in addition to continuous rate).
    bool burst_on_message = false;
};

struct OscParticleEmitEvent {
    std::uint64_t sequence = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    bool has_z = false;
    std::uint32_t count = 8;
};

inline ParticleEmitter makeOscParticleEmitter() {
    return makeOscTrackerBubbleEmitter();
}

inline glm::vec3 oscParticleWorldPosition(const OscParticleEmitSettings& settings,
                                          const OscParticleEmitEvent& event) {
    const float a = event.x * settings.scale + settings.offset.x;
    const float b = event.y * settings.scale + settings.offset.y;
    switch (settings.plane) {
    case OscParticlePlane::XY:
        return glm::vec3(a, b, event.has_z ? event.z : settings.plane_height);
    case OscParticlePlane::XZ:
    default:
        return glm::vec3(a, event.has_z ? event.z : settings.plane_height, b);
    }
}

bool tryParseOscParticleEmit(const OscMessage& message, const OscParticleEmitSettings& settings,
                             OscParticleEmitEvent& out_event);

} // namespace genesis
