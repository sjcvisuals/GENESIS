#pragma once

#include "scene/SceneMaterial.hpp"

#include <cstring>
#include <cstdint>

namespace genesis {

// Sentinel: no optional map bound (shader skips sampling).
inline constexpr std::uint32_t kInvalidTextureIndex = 0xFFFFFFFFu;

// 128-byte push block — OpenPBR realtime subset + bindless texture indices.
// Vulkan guarantees at least 128 bytes of push-constant storage.
struct MeshMaterialPushConstants {
    float base_color[4];       // OpenPBR base_color.rgb + geometry_opacity
    float emissive[4];         // emission_color * luminance, a = normal_scale
    float material_params[4];  // x metalness, y specular_roughness, z alphaMode, w alphaCutoff
    std::uint32_t instance_base = 0;
    std::uint32_t texture_index = 0;
    std::uint32_t mr_texture_index = kInvalidTextureIndex;
    std::uint32_t packed_maps = kInvalidTextureIndex; // low16=normal, high16=emissive

    // OpenPBR extension lobe controls (offset 64).
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
};

static_assert(sizeof(MeshMaterialPushConstants) == 128, "MeshMaterialPushConstants size mismatch");

inline std::uint32_t packMapIndices(std::uint32_t normal_index, std::uint32_t emissive_index) {
    const std::uint32_t n = (normal_index == kInvalidTextureIndex) ? 0xFFFFu : (normal_index & 0xFFFFu);
    const std::uint32_t e =
        (emissive_index == kInvalidTextureIndex) ? 0xFFFFu : (emissive_index & 0xFFFFu);
    return n | (e << 16);
}

inline MeshMaterialPushConstants makeMeshMaterialPushConstants(const SceneMaterial& material) {
    MeshMaterialPushConstants pc{};
    // OpenPBR base_weight scales the base substrate contribution.
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
    pc.material_params[2] = static_cast<float>(static_cast<int>(material.alpha_mode));
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
    // Bake OpenPBR coat_darkening into coat absorption color (1 = physical, <1 lightens darkening).
    if (material.coat_darkening < 0.999f) {
        const float d = material.coat_darkening;
        pc.coat_color[0] = glm::mix(1.0f, pc.coat_color[0], d);
        pc.coat_color[1] = glm::mix(1.0f, pc.coat_color[1], d);
        pc.coat_color[2] = glm::mix(1.0f, pc.coat_color[2], d);
    }
    pc.fuzz_weight = material.fuzz_weight;
    pc.fuzz_color[0] = material.fuzz_color.r;
    pc.fuzz_color[1] = material.fuzz_color.g;
    pc.fuzz_color[2] = material.fuzz_color.b;
    pc.fuzz_roughness = material.fuzz_roughness;
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
