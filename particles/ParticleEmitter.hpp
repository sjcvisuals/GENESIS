#pragma once

// Tugela particle emitter (modular Soft Circles stack — not a UE source port).
// Flat enabled "modules" instead of a graph VM: spawn shape, forces, over-life curves,
// kill volumes, sprite renderer, and a Trail Renderer module (history ribbons that taper).

#include "particles/ParticleCurve.hpp"

#include <array>
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace genesis {

enum class ParticleSpawnShape : std::uint8_t {
    Point = 0,
    Sphere = 1,
    Cone = 2,
    Box = 3,
    Ring = 4, // cylinder ring in local XZ
};

enum class ParticleRenderMesh : std::uint8_t {
    Cube = 0,
    Sphere = 1,
    Sprite = 2, // camera-facing quad (sprite renderer)
};

enum class ParticleVelocityMode : std::uint8_t {
    Cone = 0,             // along spawn direction / cone
    FromPoint = 1,        // radial from emitter origin
    Random = 2,           // random unit direction
    HorizontalPlane = 3,  // unit direction in XZ (no rise) — feet / floor drift
};

struct ParticleBurst {
    float time_seconds = 0.0f; // after start_delay, within emitter age
    std::uint32_t count = 0;
};

// Authoring data stored on a scene node. Simulation state lives in ParticleWorld.
struct ParticleEmitter {
    // --- Emitter state (Tugela Emitter Properties) ---
    bool enabled = true;
    bool looping = true;
    bool world_space = true;

    float duration_seconds = 0.0f; // 0 = forever while enabled
    float start_delay_seconds = 0.0f;

    // --- Spawn Rate / Burst (Spawn modules) ---
    bool mod_spawn_rate = true; // graph stack enable
    float rate_per_second = 40.0f;
    std::uint32_t burst_count = 0;
    std::array<ParticleBurst, 4> bursts{};
    std::uint8_t burst_entries = 0;
    std::uint32_t max_particles = 4096;

    // --- Initialize (Lifetime / Size / Color / Velocity) ---
    bool mod_lifetime = true;
    bool mod_velocity = true;
    bool mod_size = true;
    bool mod_color = true;
    float lifetime_min = 1.0f;
    float lifetime_max = 2.0f;
    float speed_min = 0.5f;
    float speed_max = 1.5f;
    float size_min = 0.05f;
    float size_max = 0.12f;
    float size_end_multiplier = 0.25f;
    glm::vec4 color_start{1.0f, 0.55f, 0.15f, 1.0f};
    glm::vec4 color_end{1.0f, 0.15f, 0.02f, 0.0f};
    ParticleVelocityMode velocity_mode = ParticleVelocityMode::Cone;
    float inherit_velocity = 0.0f;

    // --- Location (Shape Location module) ---
    bool mod_shape = true;
    ParticleSpawnShape spawn_shape = ParticleSpawnShape::Cone;
    float spawn_radius = 0.1f;
    float spawn_box_height = 0.1f;
    float cone_angle_degrees = 25.0f;

    // --- Forces (Update modules) ---
    bool force_gravity = true;
    glm::vec3 gravity{0.0f, -2.0f, 0.0f};

    bool force_drag = true;
    float drag = 0.15f;

    bool force_point = false;
    glm::vec3 point_force_position{0.0f};
    float point_force_strength = 0.0f;
    float point_force_softness = 0.25f;

    bool force_vortex = false;
    float vortex_strength = 0.0f;
    glm::vec3 vortex_axis{0.0f, 1.0f, 0.0f};
    float vortex_radius = 2.0f;

    bool force_noise = false;
    float noise_strength = 0.0f;
    float noise_frequency = 1.0f;

    bool force_accel = false;
    glm::vec3 acceleration{0.0f};

    // --- Kill ---
    bool kill_plane = false;
    float kill_plane_y = -10.0f;

    bool kill_box = false;
    glm::vec3 kill_box_min{-10.0f, -10.0f, -10.0f};
    glm::vec3 kill_box_max{10.0f, 10.0f, 10.0f};

    // --- Scale / Color over Life ---
    bool mod_size_over_life = true;
    bool mod_color_over_life = true;
    ParticleCurve1 size_over_life{};
    ParticleCurveColor color_over_life{};

    // --- Scale by Speed ---
    bool scale_by_speed = false;
    float scale_by_speed_range_min = 0.0f;
    float scale_by_speed_range_max = 4.0f;
    float scale_by_speed_mul_min = 0.6f;
    float scale_by_speed_mul_max = 1.8f;

    // --- Spawn by distance ---
    float spawn_per_unit = 0.0f;

    // --- Sprite Renderer ---
    bool mod_sprite = true;
    ParticleRenderMesh render_mesh = ParticleRenderMesh::Sprite;
    bool unlit = true;
    bool additive = true; // SrcAlpha/One soft-emissive
    float sprite_aspect = 1.0f;
    // Optional VelocityAligned stretch (kept for compatibility; prefer Trail for ribbons).
    float velocity_stretch = 0.0f;

    // --- OSC Drive (optional Spawn stack module) ---
    // When on, Protocol XY-drive targets this emitter path (not a special actor type).
    bool osc_drive = false;

    // --- Trail Renderer (ribbon / wake) ---
    // Soft disc chain along motion history; widths are fractions of the head size.
    bool trail_enabled = false;
    std::uint8_t trail_segments = 10;     // history points (2..24)
    float trail_sample_distance = 0.035f; // meters between samples
    float trail_width = 0.75f;            // width at head as fraction of particle size
    float trail_width_end = 0.1f;         // width at tip (small = taper toward a point)
    float trail_alpha = 0.55f;            // trail opacity multiplier vs particle color
    float trail_lifetime = 0.85f;         // conceptual trail length in seconds (fade with age)

    std::uint64_t seed_override = 0;
    bool fluid_coupled = false;
};

inline void syncEmitterCurvesFromEndpoints(ParticleEmitter& e) {
    if (e.size_over_life.count == 0) {
        e.size_over_life = ParticleCurve1::linear(1.0f, e.size_end_multiplier);
    }
    if (e.color_over_life.count == 0) {
        e.color_over_life = ParticleCurveColor::linear(e.color_start, e.color_end);
    }
}

inline void rebuildEmitterCurvesFromEndpoints(ParticleEmitter& e) {
    e.size_over_life = ParticleCurve1::linear(1.0f, e.size_end_multiplier);
    e.color_over_life = ParticleCurveColor::linear(e.color_start, e.color_end);
}

inline ParticleEmitter makeDefaultParticleEmitter() {
    ParticleEmitter e{};
    syncEmitterCurvesFromEndpoints(e);
    return e;
}

// Gentle soft circles rising with a tapering trail (default Tugela demo).
inline ParticleEmitter makeGentleSoftEmitter() {
    ParticleEmitter e = makeDefaultParticleEmitter();
    // Sparse soft discs — round head + quieter tapering trail (stay in frame).
    e.rate_per_second = 8.0f;
    e.max_particles = 200;
    e.lifetime_min = 4.0f;
    e.lifetime_max = 6.0f;
    e.speed_min = 0.18f; // slow rise so trails + heads stay on camera
    e.speed_max = 0.3f;
    e.size_min = 0.075f;
    e.size_max = 0.1f;
    e.size_end_multiplier = 0.45f;
    e.color_start = glm::vec4(0.95f, 0.92f, 0.55f, 0.82f); // soft yellow base; tint → green/blue
    e.color_end = glm::vec4(0.40f, 0.72f, 1.0f, 0.0f);     // blue fade over life
    e.force_gravity = true;
    e.gravity = glm::vec3(0.0f, 0.28f, 0.0f); // gentle +Y lift only
    e.force_drag = true;
    e.drag = 0.22f;
    e.spawn_shape = ParticleSpawnShape::Sphere;
    e.spawn_radius = 0.16f;
    e.spawn_box_height = 0.05f;
    e.cone_angle_degrees = 5.0f;
    e.velocity_mode = ParticleVelocityMode::Cone;
    e.render_mesh = ParticleRenderMesh::Sprite;
    e.additive = true; // self-illuminated glow on black stage
    e.unlit = true;
    e.sprite_aspect = 1.0f;
    e.velocity_stretch = 0.0f;
    e.force_noise = false;
    e.trail_enabled = true;
    e.trail_segments = 14;            // history length for tip→neck wedge ribbon
    e.trail_sample_distance = 0.04f;  // ~0.5m wake behind rising head
    e.trail_width = 0.78f;            // neck under soft-circle core (clean trail→ball join)
    e.trail_width_end = 0.1f;         // authored tip feel (wedge texture owns silhouette)
    e.trail_alpha = 0.95f;
    e.trail_lifetime = 3.0f;
    syncEmitterCurvesFromEndpoints(e);
    e.color_over_life.count = 3;
    e.color_over_life.keys[0] = {0.0f, glm::vec4(0.95f, 0.88f, 0.35f, 0.35f)}; // yellow birth
    e.color_over_life.keys[1] = {0.35f, glm::vec4(0.45f, 0.95f, 0.55f, 0.8f)}; // green mid
    e.color_over_life.keys[2] = {1.0f, glm::vec4(0.35f, 0.68f, 1.0f, 0.0f)};   // blue fade
    e.size_over_life.count = 3;
    e.size_over_life.keys[0] = {0.0f, 0.9f};
    e.size_over_life.keys[1] = {0.2f, 1.0f};
    e.size_over_life.keys[2] = {1.0f, 0.4f};
    return e;
}

// Soft circles without trails (module stack base).
inline ParticleEmitter makeGentleCirclesEmitter() {
    ParticleEmitter e = makeGentleSoftEmitter();
    e.trail_enabled = false;
    e.rate_per_second = 14.0f;
    return e;
}

inline ParticleEmitter makeBubbleRingEmitter() {
    return makeGentleSoftEmitter();
}

inline ParticleEmitter makeOscTrackerBubbleEmitter() {
    ParticleEmitter e = makeGentleSoftEmitter();
    e.rate_per_second = 6.0f;
    e.max_particles = 180;
    e.lifetime_min = 3.0f;
    e.lifetime_max = 4.5f;
    e.speed_min = 0.26f;
    e.speed_max = 0.4f;
    e.size_min = 0.065f;
    e.size_max = 0.088f;
    e.spawn_shape = ParticleSpawnShape::Point;
    e.spawn_radius = 0.04f;
    e.cone_angle_degrees = 6.0f;
    e.spawn_per_unit = 8.0f;
    e.inherit_velocity = 0.1f; // keep rise dominant over lateral OSC drift
    e.gravity = glm::vec3(0.0f, 0.45f, 0.0f);
    e.drag = 0.2f;
    e.force_noise = false;
    e.additive = false;
    e.velocity_stretch = 0.0f;
    e.trail_enabled = true;
    e.trail_segments = 10;
    e.trail_sample_distance = 0.04f;
    e.trail_width = 0.6f;
    e.trail_width_end = 0.12f;
    e.trail_alpha = 0.85f;
    e.trail_lifetime = 2.6f;
    syncEmitterCurvesFromEndpoints(e);
    e.color_over_life.count = 3;
    e.color_over_life.keys[0] = {0.0f, glm::vec4(0.9f, 0.94f, 1.0f, 0.22f)};
    e.color_over_life.keys[1] = {0.15f, glm::vec4(0.92f, 0.95f, 1.0f, 0.65f)};
    e.color_over_life.keys[2] = {1.0f, glm::vec4(0.65f, 0.78f, 0.92f, 0.0f)};
    return e;
}

// Compatibility aliases (old arc/lime names → gentle look).
inline ParticleEmitter makeArcCyanSparkEmitter() {
    return makeGentleSoftEmitter();
}
inline ParticleEmitter makeArcLimeStreakEmitter() {
    return makeGentleCirclesEmitter();
}

} // namespace genesis
