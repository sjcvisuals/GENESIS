#pragma once

#include "scene/MeshBounds.hpp"
#include "scene/Scene.hpp"
#include "renderer/Mesh.hpp"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace genesis {

struct PickRay {
    glm::vec3 origin{};
    glm::vec3 direction{};
};

struct ScenePickParams {
    bool meshes_only = false;
    bool exclude_cameras = false;
    float bounds_slack_m = 0.002f;
    float min_thin_extent_m = 0.01f;
};

struct ScenePickResult {
    NodeId node_id = 0;
    float distance = 0.0f;
};

inline bool rayIntersectsWorldAabb(const PickRay& ray, const glm::vec3& aabb_min,
                                   const glm::vec3& aabb_max, float& out_distance) {
    float t_min = 0.0f;
    float t_max = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(ray.direction[axis]) < 1e-8f) {
            if (ray.origin[axis] < aabb_min[axis] || ray.origin[axis] > aabb_max[axis]) {
                return false;
            }
            continue;
        }

        const float inv_d = 1.0f / ray.direction[axis];
        float t0 = (aabb_min[axis] - ray.origin[axis]) * inv_d;
        float t1 = (aabb_max[axis] - ray.origin[axis]) * inv_d;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        t_min = std::max(t_min, t0);
        t_max = std::min(t_max, t1);
        if (t_max < t_min) {
            return false;
        }
    }

    out_distance = t_min;
    return t_max >= 0.0f;
}

inline void expandThinWorldAabb(glm::vec3& aabb_min, glm::vec3& aabb_max, float min_extent,
                                float slack) {
    const glm::vec3 extent = aabb_max - aabb_min;
    for (int axis = 0; axis < 3; ++axis) {
        if (extent[axis] >= min_extent) {
            continue;
        }
        const float pad = std::max(slack, (min_extent - extent[axis]) * 0.5f);
        aabb_min[axis] -= pad;
        aabb_max[axis] += pad;
    }
}

inline float worldAabbVolume(const glm::vec3& aabb_min, const glm::vec3& aabb_max) {
    const glm::vec3 extent = aabb_max - aabb_min;
    return extent.x * extent.y * extent.z;
}

inline bool nodeWorldPickAabb(const Node& node, const Mesh* mesh, const ScenePickParams& params,
                              glm::vec3& out_min, glm::vec3& out_max) {
    if (mesh != nullptr) {
        worldAabbCorners(node.world, mesh->local_aabb_min, mesh->local_aabb_max, out_min, out_max);
        expandThinWorldAabb(out_min, out_max, params.min_thin_extent_m, params.bounds_slack_m);
        return true;
    }

    if (!node.light.has_value() && !node.camera.has_value()) {
        return false;
    }

    const glm::vec3 center = glm::vec3(node.world[3]);
    const glm::vec3 scale = worldScaleFromMatrix(node.world);
    const float max_axis = std::max(scale.x, std::max(scale.y, scale.z));
    const float half_extent = std::max(0.15f, 0.25f * max_axis);
    out_min = center - glm::vec3(half_extent);
    out_max = center + glm::vec3(half_extent);
    return true;
}

inline bool isScenePickCandidate(const Node& node, const ScenePickParams& params) {
    if (!node.visible || !node.pickable) {
        return false;
    }
    if (params.exclude_cameras && node.camera.has_value()) {
        return false;
    }
    if (params.meshes_only) {
        return node.mesh_resource.has_value();
    }
    return node.mesh_resource.has_value() || node.light.has_value() || node.camera.has_value();
}

inline std::optional<ScenePickResult> pickNodeAtRay(const Scene& scene,
                                                    const std::vector<Mesh>& meshes,
                                                    const PickRay& ray,
                                                    const ScenePickParams& params = {}) {
    std::optional<ScenePickResult> best;
    float best_volume = std::numeric_limits<float>::max();
    constexpr float kDistanceEpsilon = 1e-4f;

    for (const Node& node : scene.nodes) {
        if (!isScenePickCandidate(node, params)) {
            continue;
        }

        const Mesh* mesh = nullptr;
        if (node.mesh_resource.has_value()) {
            const std::uint32_t mesh_id = *node.mesh_resource;
            if (mesh_id >= meshes.size()) {
                continue;
            }
            mesh = &meshes[mesh_id];
        }

        glm::vec3 aabb_min{};
        glm::vec3 aabb_max{};
        if (!nodeWorldPickAabb(node, mesh, params, aabb_min, aabb_max)) {
            continue;
        }

        float distance = 0.0f;
        if (!rayIntersectsWorldAabb(ray, aabb_min, aabb_max, distance)) {
            continue;
        }

        const float volume = worldAabbVolume(aabb_min, aabb_max);
        if (!best.has_value() || distance + kDistanceEpsilon < best->distance) {
            best = ScenePickResult{node.id, distance};
            best_volume = volume;
            continue;
        }

        if (std::abs(distance - best->distance) <= kDistanceEpsilon && volume < best_volume) {
            best = ScenePickResult{node.id, distance};
            best_volume = volume;
        }
    }

    return best;
}

} // namespace genesis
