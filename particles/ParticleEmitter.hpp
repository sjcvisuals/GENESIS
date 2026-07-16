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
    std::uint32_t max_particles = 512;

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
    float spawn_radius = 0.1f;       // sphere / cone base / box half-extent xy
    float spawn_box_height = 0.1f;   // box half-extent y
    float cone_angle_degrees = 25.0f;

    ParticleRenderMesh render_mesh = ParticleRenderMesh::Cube;
    bool unlit = true; // emissive-looking particles (high emissive from color)

    // 0 = inherit scene_seed from ParticleWorld; otherwise overrides.
    std::uint64_t seed_override = 0;
};

inline ParticleEmitter makeDefaultParticleEmitter() {
    return ParticleEmitter{};
}

} // namespace genesis
