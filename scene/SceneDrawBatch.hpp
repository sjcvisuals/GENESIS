#pragma once

#include "renderer/MeshInstance.hpp"
#include "renderer/MeshDrawPushConstants.hpp"
#include "scene/SceneRuntime.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace genesis {

struct SceneMeshBatch {
    const Mesh* mesh = nullptr;
    const GpuTexture* base_color_texture = nullptr;
    std::uint32_t texture_index = 0;
    MeshMaterialPushConstants material{};
    std::vector<GpuMeshInstance> instances;
    std::vector<NodeId> node_ids;
};

inline std::uint64_t materialKey(const MeshMaterialPushConstants& material) {
    std::uint64_t hash = 14695981039346656037ull;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&material);
    for (std::size_t i = 0; i < sizeof(MeshMaterialPushConstants); ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

struct BatchKey {
    const Mesh* mesh = nullptr;
    const GpuTexture* texture = nullptr;
    std::uint64_t material_hash = 0;

    bool operator==(const BatchKey& other) const {
        return mesh == other.mesh && texture == other.texture && material_hash == other.material_hash;
    }
};

struct BatchKeyHash {
    std::size_t operator()(const BatchKey& key) const {
        std::size_t hash = std::hash<const void*>{}(key.mesh);
        hash ^= std::hash<const void*>{}(key.texture) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= key.material_hash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};

inline std::uint64_t batchSortKey(const SceneMeshBatch& batch) {
    std::uint64_t key = reinterpret_cast<std::uint64_t>(batch.mesh);
    key = (key << 16) ^ reinterpret_cast<std::uint64_t>(batch.base_color_texture);
    key = (key << 32) ^ materialKey(batch.material);
    return key;
}

inline void sortDrawBatches(std::vector<SceneMeshBatch>& batches) {
    std::sort(batches.begin(), batches.end(),
              [](const SceneMeshBatch& a, const SceneMeshBatch& b) {
                  return batchSortKey(a) < batchSortKey(b);
              });
}

inline void buildDrawBatches(const std::vector<SceneDrawCommand>& commands,
                             std::vector<SceneMeshBatch>& out_batches,
                             SceneRenderStats* out_stats = nullptr) {
    out_batches.clear();
    if (commands.empty()) {
        if (out_stats) {
            out_stats->draw_batches = 0;
        }
        return;
    }

    std::unordered_map<BatchKey, std::size_t, BatchKeyHash> batch_lookup;
    batch_lookup.reserve(commands.size());

    for (const SceneDrawCommand& command : commands) {
        MeshMaterialPushConstants material = command.material;

        const BatchKey key{command.mesh, command.base_color_texture, materialKey(material)};
        auto [it, inserted] = batch_lookup.emplace(key, out_batches.size());
        if (inserted) {
            SceneMeshBatch batch{};
            batch.mesh = command.mesh;
            batch.base_color_texture = command.base_color_texture;
            batch.material = material;
            out_batches.push_back(std::move(batch));
        }

        SceneMeshBatch& batch = out_batches[it->second];
        GpuMeshInstance instance{};
        if (command.mesh) {
            fillGpuMeshInstance(instance, command.model, command.mesh->local_aabb_min,
                                command.mesh->local_aabb_max);
        } else {
            fillGpuMeshInstance(instance, command.model, glm::vec3(-0.5f), glm::vec3(0.5f));
        }
        batch.instances.push_back(instance);
        batch.node_ids.push_back(command.node_id);
    }

    sortDrawBatches(out_batches);

    if (out_stats) {
        out_stats->draw_batches = static_cast<std::uint32_t>(out_batches.size());
    }
}

} // namespace genesis
