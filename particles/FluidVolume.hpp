#pragma once

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace genesis {

// GPU stable-fluids volume authored on a scene node.
// Simulation is a 3D grid filling local UVW [0,1]^3, mapped to extent_meters
// (width × height × depth). Rendering raymarches the density field inside the box.
struct FluidVolume {
    bool enabled = true;

    // Cubic simulation resolution (power-of-two, clamped 32–96). Higher = sharper, more GPU cost.
    std::uint32_t grid_resolution = 64;

    // World-space extent of the fluid domain (meters): X=width, Y=height, Z=depth.
    glm::vec3 extent_meters{2.0f, 2.0f, 2.0f};

    float viscosity = 0.0002f;
    float vorticity_strength = 0.35f;
    float velocity_dissipation = 0.995f;
    float density_dissipation = 0.985f;

    // Primary ink source (UVW 0–1 in volume space). Velocity is in UVW units/second.
    bool source_enabled = true;
    glm::vec3 source_uvw{0.5f, 0.12f, 0.5f};
    float source_radius = 0.06f;
    float source_density = 1.25f;
    glm::vec3 source_velocity{0.0f, 2.5f, 0.0f};

    // Secondary source for lava-lamp-style blobs and circulation.
    bool source2_enabled = false;
    glm::vec3 source2_uvw{0.45f, 0.72f, 0.45f};
    float source2_radius = 0.08f;
    float source2_density = 1.1f;
    glm::vec3 source2_velocity{0.0f, -0.4f, 0.0f};

    // Gentle UVW wobble so blobs merge and split like a lava lamp.
    bool animate_sources = false;

    glm::vec4 dye_color{0.15f, 0.55f, 1.0f, 1.0f};

    // When true, child/neighbor particle emitters inherit velocity from this field.
    bool drive_particles = true;

    std::uint32_t pressure_iterations = 16;
};

// Tall cube volume with two animated blobs — default for new fluid volumes.
inline FluidVolume makeLavaLampFluidVolume() {
    FluidVolume f{};
    f.grid_resolution = 64;
    // Tall rectangular prism (lamp body) — equal width/depth so orbiting reads as a 3D volume.
    f.extent_meters = {0.70f, 1.45f, 0.70f};
    f.velocity_dissipation = 0.992f;
    f.density_dissipation = 0.996f;
    f.pressure_iterations = 16;
    f.vorticity_strength = 0.45f;

    // Bottom heater: warm wax rises as distinct blobs near the center column.
    f.source_enabled = true;
    f.source_uvw = {0.50f, 0.10f, 0.50f};
    f.source_radius = 0.09f;
    f.source_density = 4.2f;
    f.source_velocity = {0.0f, 0.35f, 0.0f};

    // Top cooler: descending wax with a slight Z offset for 3D circulation.
    f.source2_enabled = true;
    f.source2_uvw = {0.40f, 0.82f, 0.55f};
    f.source2_radius = 0.11f;
    f.source2_density = 3.0f;
    f.source2_velocity = {0.06f, -0.18f, -0.04f};

    f.animate_sources = true;
    f.dye_color = {1.0f, 0.38f, 0.06f, 1.0f};
    f.drive_particles = true;
    return f;
}

inline FluidVolume makeDefaultFluidVolume() {
    return makeLavaLampFluidVolume();
}

} // namespace genesis
