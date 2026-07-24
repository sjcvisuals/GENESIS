#pragma once

#include "scene/SceneMaterial.hpp"

namespace genesis {

// Engine default OpenPBR material: ambientCG Metal019 (CC0), installed under
// assets/textures/default_pbr/. Texture paths are relative to assets/textures/.
inline SceneMaterial makeDefaultPbrMaterial() {
    SceneMaterial material;
    material.base_weight = 1.0f;
    material.base_color = glm::vec4(1.0f);
    material.metallic = 1.0f;
    material.roughness = 1.0f;
    material.specular_weight = 1.0f;
    material.specular_color = glm::vec3(1.0f);
    material.specular_ior = 1.5f;
    material.normal_scale = 1.0f;
    material.base_color_texture = "default_pbr/base_color.jpg";
    material.metallic_roughness_texture = "default_pbr/metallic_roughness.jpg";
    material.normal_texture = "default_pbr/normal.jpg";
    return material;
}

} // namespace genesis
