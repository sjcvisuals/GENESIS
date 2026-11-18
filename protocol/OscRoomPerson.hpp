#pragma once

// Room-scale OSC person → Tugela particle mapping (dual-ring cylindrical room).
// Default address: /genesis/room/person
//   ,ff      x z                 (legacy single-person → id 0)
//   ,fff     x y z | x z count   (legacy; id 0)
//   ,iff     id x z              (preferred: person id + floor XZ)
//   ,ifff    id x y z            (id + feet height)
//   ,iffff   id x y z count
//
// Ambient rings:
//   Outer ~6 m diameter — rise / float upward
//   Middle ~2 m diameter — flow outward on the floor plane (no rise)
//
// Per-person pool (max 10 active; capacity 20):
//   Each ID → Feet + Wall Tugela emitters (pre-created). Idle pairs park at origin.

#include "particles/ParticleEmitter.hpp"
#include "protocol/OscTypes.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <glm/vec3.hpp>
#include <string>

namespace genesis {

inline constexpr std::size_t kRoomPersonSlotCapacity = 20;

struct OscRoomPersonSettings {
    bool enabled = true;
    std::string address = "/genesis/room/person";

    // Pre-created pool under this empty. Feet/Wall leaves: "Feet 00" / "Wall 00".
    std::string person_pool_path = "Scene/Person Pool";
    // Legacy single-emitter paths (still created if pool missing / old scenes).
    std::string feet_emitter_path = "Scene/Person Pool/Feet 00";
    std::string wall_emitter_path = "Scene/Person Pool/Wall 00";
    std::string ray_emitter_path = "Scene/Particle System/Center Ray";

    // Active person slots (design toward capacity 20).
    std::uint32_t max_persons = 10;
    float slot_idle_seconds = 0.75f;

    // Cylindrical outer wall / outer particle ring (meters). Diameter ≈ 6 m.
    float room_radius = 3.0f;
    // Inner tank-base ring (meters). Diameter ≈ 2 m.
    float tank_radius = 1.0f;
    // Legacy AABB half-extents kept in sync with room_radius for older UI/bindings.
    float room_half_x = 3.0f;
    float room_half_z = 3.0f;
    glm::vec3 room_center{0.0f, 0.0f, 0.0f};
    float feet_height = 0.05f;
    float wall_spawn_height = 0.12f;

    bool track_feet_emitter = true;
    bool track_wall_emitter = true;
    // Continuous rate on pooled emitters is preferred; bursts optional.
    bool burst_at_feet = false;
    bool burst_along_ray = false;
    bool burst_at_wall = false;

    std::uint32_t feet_burst = 8;
    std::uint32_t wall_burst = 8;
    std::uint32_t ray_samples = 0;
    std::uint32_t ray_burst_each = 2;
    std::uint32_t max_burst = 48;
};

struct OscRoomPersonEvent {
    std::uint64_t sequence = 0;
    std::int32_t person_id = 0;
    float x = 0.0f;
    float z = 0.0f;
    float y = 0.0f;
    bool has_y = false;
    std::uint32_t count = 0; // 0 → use settings defaults
    double received_seconds = 0.0;
};

struct OscRoomPersonPose {
    glm::vec3 feet{0.0f};
    glm::vec3 wall{0.0f};
    glm::vec3 center{0.0f};
};

struct OscRoomPersonSlot {
    std::int32_t person_id = -1; // -1 = free / idle at origin
    double last_seen_seconds = 0.0;
    bool occupied = false;
};

inline std::string roomPersonFeetPath(const OscRoomPersonSettings& settings, std::size_t slot) {
    char leaf[32];
    std::snprintf(leaf, sizeof(leaf), "Feet %02zu", slot);
    return settings.person_pool_path + "/" + leaf;
}

inline std::string roomPersonWallPath(const OscRoomPersonSettings& settings, std::size_t slot) {
    char leaf[32];
    std::snprintf(leaf, sizeof(leaf), "Wall %02zu", slot);
    return settings.person_pool_path + "/" + leaf;
}

inline void configureRoomOscInstallSettings(OscRoomPersonSettings& room) {
    room.enabled = true;
    room.address = "/genesis/room/person";
    room.person_pool_path = "Scene/Person Pool";
    room.feet_emitter_path = "Scene/Person Pool/Feet 00";
    room.wall_emitter_path = "Scene/Person Pool/Wall 00";
    room.ray_emitter_path = "Scene/Particle System/Center Ray";
    room.max_persons = 10;
    room.slot_idle_seconds = 0.75f;
    room.room_radius = 3.0f;
    room.tank_radius = 1.0f;
    room.room_half_x = room.room_radius;
    room.room_half_z = room.room_radius;
    room.burst_at_feet = false;
    room.burst_at_wall = false;
    room.burst_along_ray = false;
    room.ray_samples = 0;
}

bool tryParseOscRoomPerson(const OscMessage& message, const OscRoomPersonSettings& settings,
                           OscRoomPersonEvent& out_event);

// Ray from room center through the person to the cylindrical outer wall (3 m default).
OscRoomPersonPose oscRoomPersonPose(const OscRoomPersonSettings& settings,
                                    const OscRoomPersonEvent& event);

inline ParticleEmitter makeRoomOuterRingEmitter() {
    ParticleEmitter e = makeGentleSoftEmitter();
    // Ambient Soft Circles on ~6 m diameter outer perimeter — float upward.
    e.rate_per_second = 5.0f;
    e.max_particles = 180;
    e.lifetime_min = 5.0f;
    e.lifetime_max = 7.5f;
    e.speed_min = 0.12f;
    e.speed_max = 0.22f;
    e.size_min = 0.06f;
    e.size_max = 0.085f;
    e.spawn_shape = ParticleSpawnShape::Ring;
    e.spawn_radius = 3.0f; // 6 m diameter
    e.spawn_box_height = 0.06f;
    e.cone_angle_degrees = 6.0f;
    e.velocity_mode = ParticleVelocityMode::Cone;
    e.force_gravity = true;
    e.gravity = glm::vec3(0.0f, 0.22f, 0.0f);
    e.drag = 0.25f;
    e.additive = true;
    e.trail_enabled = true;
    e.trail_segments = 12;
    e.trail_sample_distance = 0.045f;
    e.trail_width = 0.85f;
    e.trail_width_end = 0.1f;
    e.trail_alpha = 0.75f;
    syncEmitterCurvesFromEndpoints(e);
    return e;
}

// Back-compat alias used by older call sites / docs.
inline ParticleEmitter makeRoomAmbientEmitter() {
    return makeRoomOuterRingEmitter();
}

inline ParticleEmitter makeRoomInnerTankRingEmitter() {
    ParticleEmitter e = makeGentleSoftEmitter();
    // Ambient Soft Circles on ~2 m diameter tank-base ring — outward on the floor (no rise).
    e.rate_per_second = 5.0f;
    e.max_particles = 140;
    e.lifetime_min = 4.0f;
    e.lifetime_max = 6.0f;
    e.speed_min = 0.18f;
    e.speed_max = 0.32f;
    e.size_min = 0.055f;
    e.size_max = 0.08f;
    e.spawn_shape = ParticleSpawnShape::Ring;
    e.spawn_radius = 1.0f; // 2 m diameter
    e.spawn_box_height = 0.02f;
    e.cone_angle_degrees = 8.0f;
    e.velocity_mode = ParticleVelocityMode::FromPoint; // radial outward from center
    e.force_gravity = false;
    e.gravity = glm::vec3(0.0f);
    e.force_drag = true;
    e.drag = 0.18f;
    e.additive = true;
    e.trail_enabled = true;
    e.trail_segments = 10;
    e.trail_sample_distance = 0.04f;
    e.trail_width = 0.8f;
    e.trail_width_end = 0.1f;
    e.trail_alpha = 0.7f;
    syncEmitterCurvesFromEndpoints(e);
    return e;
}

inline ParticleEmitter makeRoomPersonFeetEmitter() {
    ParticleEmitter e = makeGentleSoftEmitter();
    // Circle at feet; horizontal cylinder drift (no upward rise).
    e.rate_per_second = 12.0f;
    e.max_particles = 160;
    e.lifetime_min = 2.5f;
    e.lifetime_max = 4.0f;
    e.speed_min = 0.16f;
    e.speed_max = 0.28f;
    e.size_min = 0.05f;
    e.size_max = 0.075f;
    e.spawn_shape = ParticleSpawnShape::Ring;
    e.spawn_radius = 0.28f;
    e.spawn_box_height = 0.02f;
    e.cone_angle_degrees = 12.0f;
    e.velocity_mode = ParticleVelocityMode::HorizontalPlane;
    e.force_gravity = false;
    e.gravity = glm::vec3(0.0f);
    e.force_drag = true;
    e.drag = 0.22f;
    e.spawn_per_unit = 0.0f;
    e.additive = true;
    e.trail_enabled = true;
    e.trail_segments = 8;
    e.trail_sample_distance = 0.035f;
    e.trail_width = 0.75f;
    e.trail_width_end = 0.1f;
    e.trail_alpha = 0.7f;
    syncEmitterCurvesFromEndpoints(e);
    return e;
}

inline ParticleEmitter makeRoomWallTraceEmitter() {
    ParticleEmitter e = makeGentleSoftEmitter();
    // Wall hit at 3 m radius — rise like the outer ring.
    e.rate_per_second = 10.0f;
    e.max_particles = 160;
    e.lifetime_min = 4.5f;
    e.lifetime_max = 6.5f;
    e.speed_min = 0.12f;
    e.speed_max = 0.22f;
    e.size_min = 0.055f;
    e.size_max = 0.08f;
    e.spawn_shape = ParticleSpawnShape::Sphere;
    e.spawn_radius = 0.16f;
    e.spawn_box_height = 0.04f;
    e.cone_angle_degrees = 8.0f;
    e.velocity_mode = ParticleVelocityMode::Cone;
    e.force_gravity = true;
    e.gravity = glm::vec3(0.0f, 0.22f, 0.0f);
    e.force_drag = true;
    e.drag = 0.25f;
    e.additive = true;
    e.trail_enabled = true;
    e.trail_segments = 11;
    e.trail_sample_distance = 0.042f;
    e.trail_width = 0.82f;
    e.trail_width_end = 0.1f;
    e.trail_alpha = 0.75f;
    syncEmitterCurvesFromEndpoints(e);
    return e;
}

inline ParticleEmitter makeRoomCenterRayEmitter() {
    ParticleEmitter e = makeGentleSoftEmitter();
    e.rate_per_second = 0.0f; // optional burst-driven samples
    e.max_particles = 120;
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
    e.additive = true;
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
