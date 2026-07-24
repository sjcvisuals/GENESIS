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

// Authoring material aligned with OpenPBR Surface (ASWF / Adobe / Autodesk).
// Spec: https://academysoftwarefoundation.github.io/OpenPBR/
//
// glTF metallic-roughness maps directly onto:
//   metallic  -> base_metalness
//   roughness -> specular_roughness
//   emissive * emissive_strength -> emission_color * emission_luminance
// Extra OpenPBR lobes (specular weight/IOR/color, coat, fuzz) make materials
// match Substance / other OpenPBR apps more closely in realtime.
struct SceneMaterial {
    // --- Base substrate ---
    float base_weight = 1.0f;
    glm::vec4 base_color{1.0f}; // rgb + geometry_opacity in .a
    float metallic = 0.0f;      // OpenPBR base_metalness
    float base_diffuse_roughness = 0.0f;

    // --- Specular (microfacet; shared by dielectric + metal) ---
    float specular_weight = 1.0f;
    glm::vec3 specular_color{1.0f};
    float roughness = 0.5f; // OpenPBR specular_roughness (glTF roughnessFactor)
    float specular_ior = 1.5f;

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

    // --- Emission (below coat) ---
    glm::vec3 emissive{0.0f}; // OpenPBR emission_color
    float emissive_strength = 1.0f; // OpenPBR emission_luminance scale

    // --- Geometry ---
    float normal_scale = 1.0f;
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
