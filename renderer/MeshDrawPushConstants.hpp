#pragma once

#include "scene/SceneMaterial.hpp"

#include <cstring>
#include <cstdint>

namespace genesis {

struct MeshMaterialPushConstants {
    float base_color[4];
    float emissive[4];
    float material_params[4]; // x metallic, y roughness, z alphaMode, w alphaCutoff
    std::uint32_t instance_base = 0;
    std::uint32_t texture_index = 0;
    std::uint32_t _pad1 = 0;
    std::uint32_t _pad2 = 0;
};

static_assert(sizeof(MeshMaterialPushConstants) == 64, "MeshMaterialPushConstants size mismatch");

inline MeshMaterialPushConstants makeMeshMaterialPushConstants(const SceneMaterial& material) {
    MeshMaterialPushConstants pc{};
    pc.base_color[0] = material.base_color.r;
    pc.base_color[1] = material.base_color.g;
    pc.base_color[2] = material.base_color.b;
    pc.base_color[3] = material.base_color.a;

    pc.emissive[0] = material.emissive.r * material.emissive_strength;
    pc.emissive[1] = material.emissive.g * material.emissive_strength;
    pc.emissive[2] = material.emissive.b * material.emissive_strength;
    pc.emissive[3] = material.emissive_strength;

    pc.material_params[0] = material.metallic;
    pc.material_params[1] = material.roughness;
    pc.material_params[2] = static_cast<float>(static_cast<int>(material.alpha_mode));
    pc.material_params[3] = material.alpha_cutoff;
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
