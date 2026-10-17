#pragma once

// Niagara-inspired particle emitter (public concepts only — not a UE source port).
// Flat enabled "modules" instead of a graph VM: spawn shape, forces, over-life curves,
// kill volumes, sprite/mesh renderer. Designed for many CPU particles + GPU instancing.

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
    Sprite = 2, // camera-facing quad (Niagara SpriteRenderer analogue)
};

enum class ParticleVelocityMode : std::uint8_t {
    Cone = 0,      // along spawn direction / cone
    FromPoint = 1, // radial from emitter origin
    Random = 2,    // random unit direction
};

struct ParticleBurst {
    float time_seconds = 0.0f; // after start_delay, within emitter age
    std::uint32_t count = 0;
};

// Authoring data stored on a scene node. Simulation state lives in ParticleWorld.
struct ParticleEmitter {
    // --- Emitter state (Niagara Emitter Properties) ---
    bool enabled = true;
    bool looping = true;
    bool world_space = true;

    float duration_seconds = 0.0f; // 0 = forever while enabled
    float start_delay_seconds = 0.0f;

    // --- Spawn Rate / Burst (Spawn modules) ---
    float rate_per_second = 40.0f;
    std::uint32_t burst_count = 0; // convenience: fires at t=0 (also mirrored in bursts[0] if set)
    std::array<ParticleBurst, 4> bursts{};
    std::uint8_t burst_entries = 0; // additional timed bursts beyond burst_count
    std::uint32_t max_particles = 4096;

    // --- Initialize (Lifetime / Size / Color / Velocity) ---
    float lifetime_min = 1.0f;
    float lifetime_max = 2.0f;
    float speed_min = 0.5f;
    float speed_max = 1.5f;
    float size_min = 0.05f;
    float size_max = 0.12f;
    float size_end_multiplier = 0.25f; // used when size_over_life.count == 0
    glm::vec4 color_start{1.0f, 0.55f, 0.15f, 1.0f};
    glm::vec4 color_end{1.0f, 0.15f, 0.02f, 0.0f};
    ParticleVelocityMode velocity_mode = ParticleVelocityMode::Cone;
    float inherit_velocity = 0.0f; // 0..1 blend of emitter motion into birth velocity

    // --- Location (Shape Location module) ---
    ParticleSpawnShape spawn_shape = ParticleSpawnShape::Cone;
    float spawn_radius = 0.1f;
    float spawn_box_height = 0.1f;
    float cone_angle_degrees = 25.0f;

    // --- Forces (Update modules, toggled like Niagara stack entries) ---
    bool force_gravity = true;
    glm::vec3 gravity{0.0f, -2.0f, 0.0f};

    bool force_drag = true;
    float drag = 0.15f;

    bool force_point = false;
    glm::vec3 point_force_position{0.0f}; // world if world_space
    float point_force_strength = 0.0f;    // +attract / -repel
    float point_force_softness = 0.25f;   // meters, avoids singularity

    bool force_vortex = false;
    float vortex_strength = 0.0f;
    glm::vec3 vortex_axis{0.0f, 1.0f, 0.0f};
    float vortex_radius = 2.0f;

    bool force_noise = false;
    float noise_strength = 0.0f;
    float noise_frequency = 1.0f;

    bool force_accel = false;
    glm::vec3 acceleration{0.0f}; // Niagara Acceleration Force (constant, separate from gravity)

    // --- Kill (Kill particles in volume / below plane) ---
    bool kill_plane = false;
    float kill_plane_y = -10.0f;

    bool kill_box = false;
    glm::vec3 kill_box_min{-10.0f, -10.0f, -10.0f};
    glm::vec3 kill_box_max{10.0f, 10.0f, 10.0f};

    // --- Scale / Color over Life (curve modules) ---
    // If count==0, fall back to linear size_end_multiplier / color_start→end.
    ParticleCurve1 size_over_life{};
    ParticleCurveColor color_over_life{};

    // --- Scale by Speed (Particle Update) ---
    bool scale_by_speed = false;
    float scale_by_speed_range_min = 0.0f; // m/s → mul min
    float scale_by_speed_range_max = 4.0f; // m/s → mul max
    float scale_by_speed_mul_min = 0.6f;
    float scale_by_speed_mul_max = 1.8f;

    // --- Spawn by distance traveled (Emitter Update) ---
    float spawn_per_unit = 0.0f; // particles per meter of emitter motion (0 = off)

    // --- Renderer ---
    ParticleRenderMesh render_mesh = ParticleRenderMesh::Sprite;
    bool unlit = true;
    float sprite_aspect = 1.0f;

    std::uint64_t seed_override = 0;
    bool fluid_coupled = false; // legacy
};

inline void syncEmitterCurvesFromEndpoints(ParticleEmitter& e) {
    if (e.size_over_life.count == 0) {
        e.size_over_life = ParticleCurve1::linear(1.0f, e.size_end_multiplier);
    }
    if (e.color_over_life.count == 0) {
        e.color_over_life = ParticleCurveColor::linear(e.color_start, e.color_end);
    }
}

// Replace authored curves with linear endpoints (inspector "Rebuild curves").
inline void rebuildEmitterCurvesFromEndpoints(ParticleEmitter& e) {
    e.size_over_life = ParticleCurve1::linear(1.0f, e.size_end_multiplier);
    e.color_over_life = ParticleCurveColor::linear(e.color_start, e.color_end);
}

inline ParticleEmitter makeDefaultParticleEmitter() {
    ParticleEmitter e{};
    syncEmitterCurvesFromEndpoints(e);
    return e;
}

inline ParticleEmitter makeBubbleRingEmitter() {
    ParticleEmitter e = makeDefaultParticleEmitter();
    e.rate_per_second = 120.0f;
    e.max_particles = 4096;
    e.lifetime_min = 2.5f;
    e.lifetime_max = 4.5f;
    e.speed_min = 0.35f;
    e.speed_max = 0.75f;
    e.size_min = 0.04f;
    e.size_max = 0.09f;
    e.size_end_multiplier = 0.35f;
    e.color_start = glm::vec4(0.55f, 0.9f, 1.0f, 0.85f);
    e.color_end = glm::vec4(0.2f, 0.45f, 1.0f, 0.0f);
    e.gravity = glm::vec3(0.0f, 0.55f, 0.0f);
    e.drag = 0.35f;
    e.spawn_shape = ParticleSpawnShape::Ring;
    e.spawn_radius = 1.25f;
    e.spawn_box_height = 0.08f;
    e.cone_angle_degrees = 12.0f;
    e.render_mesh = ParticleRenderMesh::Sprite;
    e.force_noise = true;
    e.noise_strength = 0.15f;
    e.noise_frequency = 0.8f;
    e.kill_plane = true;
    e.kill_plane_y = -0.5f;
    syncEmitterCurvesFromEndpoints(e);
    // Soft fade-in then fade-out (Niagara Color over Life feel).
    e.color_over_life.count = 3;
    e.color_over_life.keys[0] = {0.0f, glm::vec4(0.55f, 0.9f, 1.0f, 0.0f)};
    e.color_over_life.keys[1] = {0.15f, glm::vec4(0.55f, 0.9f, 1.0f, 0.9f)};
    e.color_over_life.keys[2] = {1.0f, glm::vec4(0.2f, 0.45f, 1.0f, 0.0f)};
    e.size_over_life.count = 3;
    e.size_over_life.keys[0] = {0.0f, 0.4f};
    e.size_over_life.keys[1] = {0.2f, 1.0f};
    e.size_over_life.keys[2] = {1.0f, 0.35f};
    return e;
}

inline ParticleEmitter makeOscTrackerBubbleEmitter() {
    ParticleEmitter e = makeDefaultParticleEmitter();
    e.rate_per_second = 60.0f;
    e.max_particles = 2048;
    e.lifetime_min = 1.8f;
    e.lifetime_max = 3.2f;
    e.speed_min = 0.45f;
    e.speed_max = 0.95f;
    e.size_min = 0.035f;
    e.size_max = 0.08f;
    e.size_end_multiplier = 0.2f;
    e.color_start = glm::vec4(1.0f, 0.75f, 0.35f, 0.95f);
    e.color_end = glm::vec4(1.0f, 0.25f, 0.1f, 0.0f);
    e.gravity = glm::vec3(0.0f, 0.7f, 0.0f);
    e.drag = 0.4f;
    e.spawn_shape = ParticleSpawnShape::Point;
    e.cone_angle_degrees = 18.0f;
    e.render_mesh = ParticleRenderMesh::Sprite;
    e.force_noise = true;
    e.noise_strength = 0.25f;
    e.noise_frequency = 1.2f;
    syncEmitterCurvesFromEndpoints(e);
    e.color_over_life.count = 3;
    e.color_over_life.keys[0] = {0.0f, glm::vec4(1.0f, 0.85f, 0.4f, 0.0f)};
    e.color_over_life.keys[1] = {0.12f, glm::vec4(1.0f, 0.75f, 0.35f, 0.95f)};
    e.color_over_life.keys[2] = {1.0f, glm::vec4(1.0f, 0.2f, 0.05f, 0.0f)};
    return e;
}

} // namespace genesis
