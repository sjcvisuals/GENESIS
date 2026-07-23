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
    Volume = 3, // Raymarched volumetric (fluid) — never treat base texture as surface albedo
};

struct SceneMaterial {
    glm::vec4 base_color{1.0f};
    glm::vec3 emissive{0.0f};
    float emissive_strength = 1.0f;
    float metallic = 0.0f;
    float roughness = 1.0f;
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
