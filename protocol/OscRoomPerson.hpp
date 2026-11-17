#pragma once

// Room-scale OSC person → particle mapping.
// Default address: /genesis/room/person
//   ,ff     x z          (XZ floor meters, room-centered)
//   ,fff    x y z        (optional feet height on Y)
//   ,ffff   x z count    (count scales feet/ray/wall bursts)
//
// On each message:
//   1) denser soft particles at the person's feet
//   2) particles along the ray room-center → person → wall
//   3) denser particles at the wall hit in front of them
//
// Deterministic with shared scene_seed + identical OSC streams (Renderbridge).

#include "particles/ParticleEmitter.hpp"
#include "protocol/OscTypes.hpp"

#include <cstdint>
#include <glm/vec3.hpp>
#include <string>

namespace genesis {

struct OscRoomPersonSettings {
    bool enabled = true;
    std::string address = "/genesis/room/person";
    std::string feet_emitter_path = "Scene/Particle System/Person Feet";
    std::string wall_emitter_path = "Scene/Particle System/Wall Trace";
    std::string ray_emitter_path = "Scene/Particle System/Center Ray";

    // Axis-aligned room on XZ (meters). Center defaults to origin.
    float room_half_x = 4.0f;
    float room_half_z = 4.0f;
    glm::vec3 room_center{0.0f, 0.0f, 0.0f};
    float feet_height = 0.05f;
    float wall_spawn_height = 0.12f;

    bool track_feet_emitter = true;
    bool track_wall_emitter = true;
    bool burst_at_feet = true;
    bool burst_along_ray = true;
    bool burst_at_wall = true;

    std::uint32_t feet_burst = 10;
    std::uint32_t wall_burst = 12;
    std::uint32_t ray_samples = 8;     // bursts along center→wall (excluding ends)
    std::uint32_t ray_burst_each = 2;  // particles per ray sample
    std::uint32_t max_burst = 48;
};

struct OscRoomPersonEvent {
    std::uint64_t sequence = 0;
    float x = 0.0f;
    float z = 0.0f;
    float y = 0.0f;
    bool has_y = false;
    std::uint32_t count = 0; // 0 → use settings defaults
};

struct OscRoomPersonPose {
    glm::vec3 feet{0.0f};
    glm::vec3 wall{0.0f};
    glm::vec3 center{0.0f};
};

bool tryParseOscRoomPerson(const OscMessage& message, const OscRoomPersonSettings& settings,
                           OscRoomPersonEvent& out_event);

// Ray from room center through the person to the XZ AABB wall.
OscRoomPersonPose oscRoomPersonPose(const OscRoomPersonSettings& settings,
                                    const OscRoomPersonEvent& event);

inline ParticleEmitter makeRoomAmbientEmitter() {
    ParticleEmitter e = makeGentleSoftEmitter();
    // Sparse room-scale field — gentle rise, large footprint.
    e.rate_per_second = 4.0f;
    e.max_particles = 160;
    e.lifetime_min = 5.0f;
    e.lifetime_max = 7.5f;
    e.speed_min = 0.12f;
    e.speed_max = 0.22f;
    e.size_min = 0.06f;
    e.size_max = 0.085f;
    e.spawn_shape = ParticleSpawnShape::Sphere;
    e.spawn_radius = 3.2f;
    e.spawn_box_height = 0.08f;
    e.cone_angle_degrees = 8.0f;
    e.gravity = glm::vec3(0.0f, 0.22f, 0.0f);
    e.drag = 0.25f;
    e.trail_enabled = true;
    e.trail_segments = 12;
    e.trail_sample_distance = 0.045f;
    e.trail_width = 0.85f;
    e.trail_width_end = 0.1f;
    e.trail_alpha = 0.75f;
    syncEmitterCurvesFromEndpoints(e);
    return e;
}

inline ParticleEmitter makeRoomPersonFeetEmitter() {
    ParticleEmitter e = makeOscTrackerBubbleEmitter();
    e.rate_per_second = 14.0f;
    e.max_particles = 220;
    e.lifetime_min = 2.8f;
    e.lifetime_max = 4.2f;
    e.speed_min = 0.2f;
    e.speed_max = 0.35f;
    e.size_min = 0.055f;
    e.size_max = 0.08f;
    e.spawn_shape = ParticleSpawnShape::Sphere;
    e.spawn_radius = 0.22f;
    e.spawn_box_height = 0.04f;
    e.cone_angle_degrees = 10.0f;
    e.spawn_per_unit = 14.0f;
    e.gravity = glm::vec3(0.0f, 0.4f, 0.0f);
    e.trail_width = 0.9f;
    e.trail_alpha = 0.88f;
    syncEmitterCurvesFromEndpoints(e);
    return e;
}

inline ParticleEmitter makeRoomWallTraceEmitter() {
    ParticleEmitter e = makeRoomPersonFeetEmitter();
    e.rate_per_second = 10.0f;
    e.max_particles = 180;
    e.spawn_radius = 0.18f;
    e.spawn_per_unit = 10.0f;
    e.size_min = 0.05f;
    e.size_max = 0.075f;
    e.gravity = glm::vec3(0.0f, 0.35f, 0.0f);
    syncEmitterCurvesFromEndpoints(e);
    return e;
}

inline ParticleEmitter makeRoomCenterRayEmitter() {
    ParticleEmitter e = makeGentleSoftEmitter();
    e.rate_per_second = 0.0f; // ray is burst-driven from OSC
    e.max_particles = 240;
    e.lifetime_min = 2.5f;
    e.lifetime_max = 4.0f;
    e.speed_min = 0.16f;
    e.speed_max = 0.28f;
    e.size_min = 0.045f;
    e.size_max = 0.07f;
    e.spawn_shape = ParticleSpawnShape::Point;
    e.spawn_radius = 0.03f;
    e.cone_angle_degrees = 7.0f;
    e.gravity = glm::vec3(0.0f, 0.32f, 0.0f);
    e.trail_enabled = true;
    e.trail_segments = 10;
    e.trail_sample_distance = 0.04f;
    e.trail_width = 0.8f;
    e.trail_width_end = 0.1f;
    e.trail_alpha = 0.8f;
    syncEmitterCurvesFromEndpoints(e);
    return e;
}

} // namespace genesis
