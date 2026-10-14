#pragma once

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>

namespace genesis {

enum class ParticleSpawnShape : std::uint8_t {
    Point = 0,
    Sphere = 1,
    Cone = 2,
    Box = 3,
    Ring = 4, // cylinder ring in local XZ; radius = spawn_radius, height jitter = spawn_box_height
};

enum class ParticleRenderMesh : std::uint8_t {
    Cube = 0,
    Sphere = 1,
};

// Authoring data stored on a scene node. Simulation state lives in ParticleWorld.
struct ParticleEmitter {
    bool enabled = true;
    bool looping = true;
    bool world_space = true;

    float duration_seconds = 0.0f; // 0 = forever while enabled
    float start_delay_seconds = 0.0f;

    float rate_per_second = 40.0f;
    std::uint32_t burst_count = 0; // one-shot burst at start (after delay)
    std::uint32_t max_particles = 2048;

    float lifetime_min = 1.0f;
    float lifetime_max = 2.0f;

    float speed_min = 0.5f;
    float speed_max = 1.5f;

    float size_min = 0.05f;
    float size_max = 0.12f;
    float size_end_multiplier = 0.25f; // size at death relative to birth

    glm::vec4 color_start{1.0f, 0.55f, 0.15f, 1.0f};
    glm::vec4 color_end{1.0f, 0.15f, 0.02f, 0.0f};

    glm::vec3 gravity{0.0f, -2.0f, 0.0f};
    float drag = 0.15f;

    ParticleSpawnShape spawn_shape = ParticleSpawnShape::Cone;
    float spawn_radius = 0.1f;       // sphere / cone / box xz / ring radius
    float spawn_box_height = 0.1f;   // box half-extent y / ring vertical jitter
    float cone_angle_degrees = 25.0f;

    ParticleRenderMesh render_mesh = ParticleRenderMesh::Sphere;
    bool unlit = true; // emissive-looking particles (high emissive from color)

    // 0 = inherit scene_seed from ParticleWorld; otherwise overrides.
    std::uint64_t seed_override = 0;

    // Legacy: advect using a parent FluidVolume (editor no longer authors fluids).
    bool fluid_coupled = false;
};

inline ParticleEmitter makeDefaultParticleEmitter() {
    return ParticleEmitter{};
}

// Large ring of rising bubble-like particles (main show emitter).
inline ParticleEmitter makeBubbleRingEmitter() {
    ParticleEmitter e = makeDefaultParticleEmitter();
    e.enabled = true;
    e.looping = true;
    e.world_space = true;
    e.rate_per_second = 120.0f;
    e.burst_count = 0;
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
    e.gravity = glm::vec3(0.0f, 0.55f, 0.0f); // buoyancy up
    e.drag = 0.35f;
    e.spawn_shape = ParticleSpawnShape::Ring;
    e.spawn_radius = 1.25f;
    e.spawn_box_height = 0.08f;
    e.cone_angle_degrees = 12.0f;
    e.render_mesh = ParticleRenderMesh::Sphere;
    e.unlit = true;
    return e;
}

// Point emitter that follows OSC xy while continuously spawning rising bubbles.
inline ParticleEmitter makeOscTrackerBubbleEmitter() {
    ParticleEmitter e = makeDefaultParticleEmitter();
    e.enabled = true;
    e.looping = true;
    e.world_space = true;
    e.rate_per_second = 60.0f;
    e.burst_count = 0;
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
    e.render_mesh = ParticleRenderMesh::Sphere;
    e.unlit = true;
    return e;
}

} // namespace genesis
