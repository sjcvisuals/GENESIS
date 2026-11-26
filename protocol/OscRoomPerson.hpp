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
// Per-person tracking (optional; capacity 20):
//   Each active OSC id can drive Feet + Wall emitters (created lazily on message,
//   not pre-baked into the scene). Authors build their own Hierarchy / stack.

#include "particles/ParticleEmitter.hpp"
#include "protocol/OscTypes.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <vector>

namespace genesis {

inline constexpr std::size_t kRoomPersonSlotCapacity = 20;

// How the center→person wall spawn is resolved.
enum class WallRaySpawnMode : std::uint8_t {
    FixedDistance = 0,           // Math cylinder at room_radius (default / current install).
    PlaneCollision = 1,          // Scene plane hit only; falls back to fixed distance on miss.
    PreferPlaneThenDistance = 2, // Try plane path first, else fixed distance.
};

struct OscRoomPersonSettings {
    // Off by default so File → New / cold launch stay empty. Protocol panel or
    // configureRoomOscInstallSettings() turns this on (creates Person Pool).
    bool enabled = false;
    std::string address = "/genesis/room/person";

    // `/tuio/2Dcur` over the existing OSC listen path (TouchDesigner owns TUIO;
    // GENESIS is not a native TUIO UDP client). No Protocol UI clutter.
    //   set  `,sifffff` [set, id, x, y, Xvel, Yvel, m] — id + x,y (vel/m ignored)
    //   alive `,siiii…` [alive, id0, id1, …] — active session IDs (NOT a count)
    //   fseq  `,si`     [fseq, n] — frame tick (debug)
    // Normalized x,y map onto the horizontal floor (scene XZ), like feet.
    bool tuio_enabled = false;
    std::string tuio_address = "/tuio/2Dcur";
    // meters = tuio_xy * tuio_scale + tuio_offset (default scale = room_radius).
    float tuio_scale = 3.0f;
    glm::vec2 tuio_offset{0.0f, 0.0f};

    // Parent empty for lazily created Feet/Wall leaves ("Feet 00" / "Wall 00").
    std::string person_pool_path = "Scene/Person Pool";
    // Legacy / UI defaults (first slot). Not auto-spawned until OSC arrives.
    std::string feet_emitter_path = "Scene/Person Pool/Feet 00";
    std::string wall_emitter_path = "Scene/Person Pool/Wall 00";
    std::string ray_emitter_path = "Scene/Particle System/Center Ray";
    // Optional scene Plane actor (mesh faces +Y). Rotate upright for a wall.
    std::string wall_plane_path = "Scene/Wall Plane";

    // Max concurrent OSC person ids that may spawn Feet+Wall pairs (lazy).
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
    // Max ray length when testing plane collision (meters).
    float wall_ray_max_distance = 20.0f;
    WallRaySpawnMode wall_ray_mode = WallRaySpawnMode::FixedDistance;
    // Viewport overlay: center→person→wall line + hit marker. Off for Renderbridge.
    bool debug_draw_wall_ray = false;

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

// Last applied wall-ray samples for editor debug draw (not serialized).
struct OscWallRayDebugSample {
    std::int32_t person_id = -1;
    glm::vec3 center{0.0f};
    glm::vec3 person{0.0f};
    glm::vec3 wall{0.0f};
    bool used_plane_hit = false;
    bool active = false;
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
    // True when produced from `/tuio/2Dcur` set (alive list parks these slots).
    bool from_tuio = false;
};

struct OscRoomPersonPose {
    glm::vec3 feet{0.0f};
    glm::vec3 wall{0.0f};
    glm::vec3 center{0.0f};
    bool used_plane_hit = false;
};

// Infinite or finite rectangle. Plane mesh faces +Y; after transform, normal =
// world +Y. Finite hits use tangent (+X) / bitangent (+Z) half-extents.
struct WallPlane {
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 tangent{1.0f, 0.0f, 0.0f};
    glm::vec3 bitangent{0.0f, 0.0f, 1.0f};
    float half_u = 1.0e6f; // meters along tangent (∞ ≈ open)
    float half_v = 1.0e6f; // meters along bitangent
};

struct OscRoomPersonSlot {
    std::int32_t person_id = -1; // -1 = free / idle at origin
    double last_seen_seconds = 0.0;
    bool occupied = false;
    // True when this slot was assigned from TUIO set/alive (alive list parks these).
    bool from_tuio = false;
};

enum class OscTuio2DcurCommand : std::uint8_t {
    Unknown = 0,
    Set,
    Alive,
    Fseq,
};

struct OscTuio2DcurSet {
    std::int32_t session_id = 0;
    float x = 0.0f; // normalized TUIO cursor X
    float y = 0.0f; // normalized TUIO cursor Y → scene Z on floor
};

struct OscTuio2DcurAlive {
    // Active person session IDs (pool). Size = number of people; values are IDs.
    std::vector<std::int32_t> session_ids;
};

struct OscTuio2DcurFseq {
    std::int32_t frame = 0;
};

struct OscTuio2DcurParse {
    OscTuio2DcurCommand command = OscTuio2DcurCommand::Unknown;
    OscTuio2DcurSet set{};
    OscTuio2DcurAlive alive{};
    OscTuio2DcurFseq fseq{};
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
    // Capacity only — do not pre-create emitters; scene stays lean until OSC arrives.
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
    room.wall_ray_mode = WallRaySpawnMode::FixedDistance;
    room.wall_plane_path = "Scene/Wall Plane";
    room.wall_ray_max_distance = 20.0f;
    room.debug_draw_wall_ray = false;
}

// File → Examples → Medusa: dual-ring + wall-plane ray debug + TUIO 2Dcur people.
inline void configureMedusaExampleSettings(OscRoomPersonSettings& room) {
    configureRoomOscInstallSettings(room);
    room.max_persons = 10;
    room.wall_ray_mode = WallRaySpawnMode::PreferPlaneThenDistance;
    room.wall_plane_path = "Scene/Wall Plane";
    room.debug_draw_wall_ray = true;
    room.tuio_enabled = true;
    room.tuio_address = "/tuio/2Dcur";
    room.tuio_scale = room.room_radius; // ~[-1,1] → ±3 m floor
    room.tuio_offset = glm::vec2(0.0f, 0.0f);
}

bool tryParseOscRoomPerson(const OscMessage& message, const OscRoomPersonSettings& settings,
                           OscRoomPersonEvent& out_event);

// Parse one `/tuio/2Dcur` message (set / alive / fseq). Address must match settings.
bool tryParseOscTuio2Dcur(const OscMessage& message, const OscRoomPersonSettings& settings,
                          OscTuio2DcurParse& out);

// Map TUIO set x,y → room-person event on the horizontal floor (x→X, y→Z meters).
void mapTuioSetToRoomPersonEvent(const OscRoomPersonSettings& settings, const OscTuio2DcurSet& set,
                                 OscRoomPersonEvent& out_event);

// Fixed-distance cylinder hit (room_radius) from room center through person XZ.
OscRoomPersonPose oscRoomPersonPose(const OscRoomPersonSettings& settings,
                                    const OscRoomPersonEvent& event);

// Same as above, optionally intersecting an infinite wall plane (mesh +Y → world normal).
OscRoomPersonPose oscRoomPersonPose(const OscRoomPersonSettings& settings,
                                    const OscRoomPersonEvent& event,
                                    const WallPlane* wall_plane);

// Ray (origin, dir unit) vs infinite plane. Returns false if parallel / behind / beyond max_t.
bool intersectRayPlane(const glm::vec3& origin, const glm::vec3& dir, const WallPlane& plane,
                       float max_t, glm::vec3& out_hit, float* out_t = nullptr);

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
    e.spawn_distribution = ParticleSpawnDistribution::Surface;
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
    e.spawn_distribution = ParticleSpawnDistribution::Surface;
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
    e.spawn_distribution = ParticleSpawnDistribution::Surface;
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
