#pragma once

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace genesis {

// GPU stable-fluids volume authored on a scene node (2D sim extruded in local XY).
struct FluidVolume {
    bool enabled = true;

    // Simulation grid resolution (power-of-two, clamped 64–512). Higher = sharper, more GPU cost.
    std::uint32_t grid_resolution = 256;

    // World-space extent of the fluid domain (meters) in local X/Y.
    glm::vec2 extent_meters{2.0f, 2.0f};

    float viscosity = 0.0002f;
    float vorticity_strength = 0.35f;
    float velocity_dissipation = 0.995f;
    float density_dissipation = 0.985f;

    // Primary ink source (UV 0–1 in volume space). Velocity is in UV units/second.
    bool source_enabled = true;
    glm::vec2 source_uv{0.5f, 0.12f};
    float source_radius = 0.06f;
    float source_density = 1.25f;
    glm::vec2 source_velocity{0.0f, 2.5f};

    // Secondary source for lava-lamp-style blobs and circulation.
    bool source2_enabled = false;
    glm::vec2 source2_uv{0.45f, 0.72f};
    float source2_radius = 0.08f;
    float source2_density = 1.1f;
    glm::vec2 source2_velocity{0.0f, -0.4f};

    // Gentle UV wobble so blobs merge and split like a lava lamp.
    bool animate_sources = false;

    glm::vec4 dye_color{0.15f, 0.55f, 1.0f, 1.0f};

    // When true, child/neighbor particle emitters inherit velocity from this field.
    bool drive_particles = true;

    std::uint32_t pressure_iterations = 24;
};

// Tall warm preset with two animated blobs — default for new fluid volumes.
inline FluidVolume makeLavaLampFluidVolume() {
    FluidVolume f{};
    f.grid_resolution = 192;
    f.extent_meters = {0.75f, 1.35f};
    f.velocity_dissipation = 0.992f;
    f.density_dissipation = 0.996f;
    f.pressure_iterations = 28;
    f.vorticity_strength = 0.45f;

    // Bottom heater: warm wax rises as distinct blobs.
    f.source_enabled = true;
    f.source_uv = {0.50f, 0.10f};
    f.source_radius = 0.10f;
    f.source_density = 4.2f;
    f.source_velocity = {0.0f, 0.35f};

    // Top cooler: descending wax for circulation / merge-split.
    f.source2_enabled = true;
    f.source2_uv = {0.40f, 0.82f};
    f.source2_radius = 0.12f;
    f.source2_density = 3.0f;
    f.source2_velocity = {0.06f, -0.18f};

    f.animate_sources = true;
    f.dye_color = {1.0f, 0.38f, 0.06f, 1.0f};
    f.drive_particles = true;
    return f;
}

inline FluidVolume makeDefaultFluidVolume() {
    return makeLavaLampFluidVolume();
}

} // namespace genesis
