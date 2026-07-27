#pragma once

#include "scene/SceneMaterial.hpp"

#include <cstring>
#include <cstdint>

namespace genesis {

// Sentinel: no optional map bound (shader skips sampling).
inline constexpr std::uint32_t kInvalidTextureIndex = 0xFFFFFFFFu;

// 256-byte push block — full OpenPBR Surface realtime packing.
// Requires VkPhysicalDeviceLimits::maxPushConstantsSize >= 256 (common on desktop/Mesa).
struct MeshMaterialPushConstants {
    float base_color[4];       // OpenPBR base_color.rgb + geometry_opacity
    float emissive[4];         // emission_color * luminance, a = normal_scale
    float material_params[4];  // x metalness, y specular_roughness, z alphaMode, w alphaCutoff
    std::uint32_t instance_base = 0;
    std::uint32_t texture_index = 0;
    std::uint32_t mr_texture_index = kInvalidTextureIndex;
    std::uint32_t packed_maps = kInvalidTextureIndex; // low16=normal, high16=emissive

    // --- OpenPBR specular / coat / fuzz (offset 64) ---
    float specular_weight = 1.0f;
    float specular_ior = 1.5f;
    float coat_weight = 0.0f;
    float coat_roughness = 0.1f;
    float specular_color[3] = {1.0f, 1.0f, 1.0f};
    float coat_ior = 1.6f;
    float coat_color[3] = {1.0f, 1.0f, 1.0f};
    float fuzz_weight = 0.0f;
    float fuzz_color[3] = {1.0f, 1.0f, 1.0f};
    float fuzz_roughness = 0.5f;

    // --- OpenPBR transmission / subsurface / thin-film / extras (offset 128) ---
    float transmission_weight = 0.0f;
    float transmission_depth = 0.0f;
    float subsurface_weight = 0.0f;
    float subsurface_radius = 1.0f;
    float transmission_color[3] = {1.0f, 1.0f, 1.0f};
    float thin_film_weight = 0.0f;
    float subsurface_color[3] = {0.8f, 0.8f, 0.8f};
    float thin_film_thickness = 0.5f;
    float transmission_scatter[3] = {0.0f, 0.0f, 0.0f};
    float thin_film_ior = 1.4f;
    float subsurface_radius_scale[3] = {1.0f, 0.5f, 0.25f};
    float transmission_scatter_anisotropy = 0.0f;
    float transmission_dispersion_scale = 0.0f;
    float transmission_dispersion_abbe = 20.0f;
    float subsurface_scatter_anisotropy = 0.0f;
    float thin_walled = 0.0f;
    float base_diffuse_roughness = 0.0f;
    float coat_darkening = 1.0f;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
    float pad2 = 0.0f;
    float pad3 = 0.0f;
    float pad4 = 0.0f;
    float pad5 = 0.0f;
};

static_assert(sizeof(MeshMaterialPushConstants) == 256, "MeshMaterialPushConstants size mismatch");

inline std::uint32_t packMapIndices(std::uint32_t normal_index, std::uint32_t emissive_index) {
    const std::uint32_t n = (normal_index == kInvalidTextureIndex) ? 0xFFFFu : (normal_index & 0xFFFFu);
    const std::uint32_t e =
        (emissive_index == kInvalidTextureIndex) ? 0xFFFFu : (emissive_index & 0xFFFFu);
    return n | (e << 16);
}

inline MeshMaterialPushConstants makeMeshMaterialPushConstants(const SceneMaterial& material) {
    MeshMaterialPushConstants pc{};
    const float base_w = material.base_weight;
    pc.base_color[0] = material.base_color.r * base_w;
    pc.base_color[1] = material.base_color.g * base_w;
    pc.base_color[2] = material.base_color.b * base_w;
    pc.base_color[3] = material.base_color.a;

    pc.emissive[0] = material.emissive.r * material.emissive_strength;
    pc.emissive[1] = material.emissive.g * material.emissive_strength;
    pc.emissive[2] = material.emissive.b * material.emissive_strength;
    pc.emissive[3] = material.normal_scale;

    pc.material_params[0] = material.metallic;
    pc.material_params[1] = material.roughness;
    // Transmission needs blending; promote Opaque -> Blend when weight is active.
    AlphaMode alpha = material.alpha_mode;
    if (material.transmission_weight > 1e-4f && alpha == AlphaMode::Opaque) {
        alpha = AlphaMode::Blend;
    }
    pc.material_params[2] = static_cast<float>(static_cast<int>(alpha));
    pc.material_params[3] = material.alpha_cutoff;

    pc.specular_weight = material.specular_weight;
    pc.specular_ior = material.specular_ior;
    pc.coat_weight = material.coat_weight;
    pc.coat_roughness = material.coat_roughness;
    pc.specular_color[0] = material.specular_color.r;
    pc.specular_color[1] = material.specular_color.g;
    pc.specular_color[2] = material.specular_color.b;
    pc.coat_ior = material.coat_ior;
    pc.coat_color[0] = material.coat_color.r;
    pc.coat_color[1] = material.coat_color.g;
    pc.coat_color[2] = material.coat_color.b;
    pc.fuzz_weight = material.fuzz_weight;
    pc.fuzz_color[0] = material.fuzz_color.r;
    pc.fuzz_color[1] = material.fuzz_color.g;
    pc.fuzz_color[2] = material.fuzz_color.b;
    pc.fuzz_roughness = material.fuzz_roughness;

    pc.transmission_weight = material.transmission_weight;
    pc.transmission_depth = material.transmission_depth;
    pc.subsurface_weight = material.subsurface_weight;
    pc.subsurface_radius = material.subsurface_radius;
    pc.transmission_color[0] = material.transmission_color.r;
    pc.transmission_color[1] = material.transmission_color.g;
    pc.transmission_color[2] = material.transmission_color.b;
    pc.thin_film_weight = material.thin_film_weight;
    pc.subsurface_color[0] = material.subsurface_color.r;
    pc.subsurface_color[1] = material.subsurface_color.g;
    pc.subsurface_color[2] = material.subsurface_color.b;
    pc.thin_film_thickness = material.thin_film_thickness;
    pc.transmission_scatter[0] = material.transmission_scatter.r;
    pc.transmission_scatter[1] = material.transmission_scatter.g;
    pc.transmission_scatter[2] = material.transmission_scatter.b;
    pc.thin_film_ior = material.thin_film_ior;
    pc.subsurface_radius_scale[0] = material.subsurface_radius_scale.r;
    pc.subsurface_radius_scale[1] = material.subsurface_radius_scale.g;
    pc.subsurface_radius_scale[2] = material.subsurface_radius_scale.b;
    pc.transmission_scatter_anisotropy = material.transmission_scatter_anisotropy;
    pc.transmission_dispersion_scale = material.transmission_dispersion_scale;
    pc.transmission_dispersion_abbe = material.transmission_dispersion_abbe;
    pc.subsurface_scatter_anisotropy = material.subsurface_scatter_anisotropy;
    pc.thin_walled = material.thin_walled ? 1.0f : 0.0f;
    pc.base_diffuse_roughness = material.base_diffuse_roughness;
    pc.coat_darkening = material.coat_darkening;
    return pc;
}

// Legacy helper for code paths that still build per-node material payloads.
struct MeshDrawPushConstants {
    float model[16];
    float base_color[4];
    float emissive[4];
    float material_params[4];
};

inline MeshDrawPushConstants makeMeshDrawPushConstants(const float* model_col_major,
                                                       const SceneMaterial& material) {
    MeshDrawPushConstants pc{};
    if (model_col_major) {
        std::memcpy(pc.model, model_col_major, sizeof(pc.model));
    } else {
        const float identity[16] = {
            1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
        };
        std::memcpy(pc.model, identity, sizeof(pc.model));
    }

    const MeshMaterialPushConstants material_pc = makeMeshMaterialPushConstants(material);
    std::memcpy(pc.base_color, material_pc.base_color, sizeof(pc.base_color));
    std::memcpy(pc.emissive, material_pc.emissive, sizeof(pc.emissive));
    std::memcpy(pc.material_params, material_pc.material_params, sizeof(pc.material_params));
    return pc;
}

} // namespace genesis
