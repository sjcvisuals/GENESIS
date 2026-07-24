#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace genesis {

enum class AlphaMode : int {
    Opaque = 0,
    Mask = 1,
    Blend = 2,
};

// Full OpenPBR Surface authoring model (ASWF / Adobe / Autodesk).
// Spec: https://academysoftwarefoundation.github.io/OpenPBR/
//
// Realtime evaluation uses cheap, weight-gated approximations for transmission,
// subsurface, and thin-film — optional lobes cost almost nothing when weight == 0.
// Anisotropic roughness is omitted (mesh vertex format has no tangents yet).
struct SceneMaterial {
    // --- Base substrate ---
    float base_weight = 1.0f;
    glm::vec4 base_color{1.0f}; // rgb + geometry_opacity in .a
    float metallic = 0.0f;      // OpenPBR base_metalness
    float base_diffuse_roughness = 0.0f;

    // --- Specular (microfacet; shared by dielectric + metal) ---
    float specular_weight = 1.0f;
    glm::vec3 specular_color{1.0f};
    float roughness = 0.5f; // OpenPBR specular_roughness
    float specular_ior = 1.5f;

    // --- Transmission (translucent dielectric base) ---
    float transmission_weight = 0.0f;
    glm::vec3 transmission_color{1.0f};
    float transmission_depth = 0.0f; // 0 = constant tint
    glm::vec3 transmission_scatter{0.0f};
    float transmission_scatter_anisotropy = 0.0f;
    float transmission_dispersion_scale = 0.0f;
    float transmission_dispersion_abbe = 20.0f;

    // --- Subsurface (opaque dielectric mix) ---
    float subsurface_weight = 0.0f;
    glm::vec3 subsurface_color{0.8f, 0.8f, 0.8f};
    float subsurface_radius = 1.0f;
    glm::vec3 subsurface_radius_scale{1.0f, 0.5f, 0.25f};
    float subsurface_scatter_anisotropy = 0.0f;

    // --- Coat ---
    float coat_weight = 0.0f;
    glm::vec3 coat_color{1.0f};
    float coat_roughness = 0.1f;
    float coat_ior = 1.6f;
    float coat_darkening = 1.0f;

    // --- Fuzz (cloth / peach fuzz) ---
    float fuzz_weight = 0.0f;
    glm::vec3 fuzz_color{1.0f};
    float fuzz_roughness = 0.5f;

    // --- Thin film (iridescence) ---
    float thin_film_weight = 0.0f;
    float thin_film_thickness = 0.5f; // micrometers
    float thin_film_ior = 1.4f;

    // --- Emission (below coat) ---
    glm::vec3 emissive{0.0f};
    float emissive_strength = 1.0f;

    // --- Geometry ---
    float normal_scale = 1.0f;
    bool thin_walled = false;
    AlphaMode alpha_mode = AlphaMode::Opaque;
    float alpha_cutoff = 0.5f;

    // Texture paths/names for serialization and glTF labels.
    std::string base_color_texture;
    std::string metallic_roughness_texture;
    std::string normal_texture;
    std::string emissive_texture;

    // GPU texture resource indices into SceneRuntime::textures(). Empty = use defaults.
    std::optional<std::uint32_t> base_color_texture_resource;
    std::optional<std::uint32_t> metallic_roughness_texture_resource;
    std::optional<std::uint32_t> normal_texture_resource;
    std::optional<std::uint32_t> emissive_texture_resource;
};

} // namespace genesis
