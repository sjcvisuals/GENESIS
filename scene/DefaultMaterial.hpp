#pragma once

#include "scene/SceneMaterial.hpp"

namespace genesis {

// Engine default OpenPBR metal: ambientCG Metal019 (CC0) under assets/textures/default_pbr/.
// Paths are relative to assets/textures/.
inline SceneMaterial makeDefaultPbrMaterial() {
    SceneMaterial material;
    material.base_weight = 1.0f;
    material.base_color = glm::vec4(1.0f);
    material.metallic = 1.0f;
    material.roughness = 1.0f; // multiplied by map
    material.specular_weight = 1.0f;
    material.specular_color = glm::vec3(1.0f);
    material.specular_ior = 1.45f;
    material.normal_scale = 1.0f;
    // Thin clear lacquer — reads as finished metal without killing albedo.
    material.coat_weight = 0.35f;
    material.coat_color = glm::vec3(1.0f);
    material.coat_roughness = 0.08f;
    material.coat_ior = 1.5f;
    material.coat_darkening = 0.85f;
    material.base_color_texture = "default_pbr/base_color.jpg";
    material.metallic_roughness_texture = "default_pbr/metallic_roughness.jpg";
    material.normal_texture = "default_pbr/normal.jpg";
    return material;
}

// Dielectric plastic (ambientCG Plastic008 CC0).
inline SceneMaterial makeDefaultPlasticMaterial() {
    SceneMaterial material;
    material.base_weight = 1.0f;
    material.base_color = glm::vec4(1.0f);
    material.metallic = 0.0f;
    material.roughness = 1.0f;
    material.specular_weight = 1.0f;
    material.specular_color = glm::vec3(1.0f);
    material.specular_ior = 1.55f;
    material.normal_scale = 1.0f;
    material.coat_weight = 0.55f;
    material.coat_roughness = 0.06f;
    material.coat_ior = 1.5f;
    material.coat_darkening = 0.9f;
    material.base_color_texture = "default_pbr_plastic/base_color.jpg";
    material.metallic_roughness_texture = "default_pbr_plastic/metallic_roughness.jpg";
    material.normal_texture = "default_pbr_plastic/normal.jpg";
    return material;
}

// Concrete floor (ambientCG Concrete034 CC0).
inline SceneMaterial makeDefaultFloorMaterial() {
    SceneMaterial material;
    material.base_weight = 1.0f;
    material.base_color = glm::vec4(1.0f);
    material.metallic = 0.0f;
    material.roughness = 1.0f;
    material.specular_weight = 1.0f;
    material.specular_ior = 1.5f;
    material.normal_scale = 1.15f;
    material.base_diffuse_roughness = 0.35f;
    material.base_color_texture = "default_pbr_floor/base_color.jpg";
    material.metallic_roughness_texture = "default_pbr_floor/metallic_roughness.jpg";
    material.normal_texture = "default_pbr_floor/normal.jpg";
    return material;
}

} // namespace genesis
