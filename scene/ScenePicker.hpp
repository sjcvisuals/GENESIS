#pragma once

#include "scene/MeshBounds.hpp"
#include "scene/Scene.hpp"
#include "renderer/Mesh.hpp"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <deque>
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

    if (node.particle_fluid.has_value()) {
        const glm::vec3 extent = node.particle_fluid->extent_meters;
        const glm::vec3 local_min(-0.5f * extent.x, -0.5f * extent.y, -0.5f * extent.z);
        const glm::vec3 local_max(0.5f * extent.x, 0.5f * extent.y, 0.5f * extent.z);
        worldAabbCorners(node.world, local_min, local_max, out_min, out_max);
        expandThinWorldAabb(out_min, out_max, params.min_thin_extent_m, params.bounds_slack_m);
        return true;
    }

    if (node.particle_emitter.has_value()) {
        const glm::vec3 center = glm::vec3(node.world[3]);
        const float half = 0.25f;
        out_min = center - glm::vec3(half);
        out_max = center + glm::vec3(half);
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
    return node.mesh_resource.has_value() || node.light.has_value() || node.camera.has_value() ||
           node.particle_fluid.has_value() || node.particle_emitter.has_value();
}

inline std::optional<ScenePickResult> pickNodeAtRay(const Scene& scene,
                                                    const std::deque<Mesh>& meshes,
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

struct ScreenRect {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;

    void normalize() {
        if (min_x > max_x) {
            std::swap(min_x, max_x);
        }
        if (min_y > max_y) {
            std::swap(min_y, max_y);
        }
    }

    bool intersects(const ScreenRect& other) const {
        return !(max_x < other.min_x || min_x > other.max_x || max_y < other.min_y ||
                 min_y > other.max_y);
    }

    bool containsPoint(float x, float y) const {
        return x >= min_x && x <= max_x && y >= min_y && y <= max_y;
    }
};

// Project world AABB to viewport-local pixels.
// `view_proj` must use OpenGL-style clip Y (same as rayFromScreen / ImGuizmo — i.e. undo
// the Vulkan Y flip on the projection before multiplying with the view matrix).
inline bool projectWorldAabbToScreen(const glm::mat4& view_proj, const glm::vec3& aabb_min,
                                     const glm::vec3& aabb_max, float viewport_w, float viewport_h,
                                     ScreenRect& out_rect) {
    const glm::vec3 corners[8] = {
        {aabb_min.x, aabb_min.y, aabb_min.z}, {aabb_max.x, aabb_min.y, aabb_min.z},
        {aabb_min.x, aabb_max.y, aabb_min.z}, {aabb_max.x, aabb_max.y, aabb_min.z},
        {aabb_min.x, aabb_min.y, aabb_max.z}, {aabb_max.x, aabb_min.y, aabb_max.z},
        {aabb_min.x, aabb_max.y, aabb_max.z}, {aabb_max.x, aabb_max.y, aabb_max.z},
    };

    bool any_visible = false;
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;

    for (const glm::vec3& corner : corners) {
        const glm::vec4 clip = view_proj * glm::vec4(corner, 1.0f);
        if (std::abs(clip.w) <= 1e-5f) {
            continue;
        }
        const float inv_w = 1.0f / clip.w;
        const float ndc_x = clip.x * inv_w;
        const float ndc_y = clip.y * inv_w;
        // Match rayFromScreen: ndc_y = 1 - 2*(mouse_y/h)  ⇒  mouse_y = (1 - ndc_y)*0.5*h
        const float sx = (ndc_x * 0.5f + 0.5f) * viewport_w;
        const float sy = (1.0f - ndc_y) * 0.5f * viewport_h;
        if (!any_visible) {
            min_x = max_x = sx;
            min_y = max_y = sy;
            any_visible = true;
        } else {
            min_x = std::min(min_x, sx);
            min_y = std::min(min_y, sy);
            max_x = std::max(max_x, sx);
            max_y = std::max(max_y, sy);
        }
    }

    if (!any_visible) {
        return false;
    }

    out_rect = {min_x, min_y, max_x, max_y};
    return true;
}

inline void pickNodesInScreenRect(const Scene& scene, const std::deque<Mesh>& meshes,
                                  const glm::mat4& view_proj, float viewport_w, float viewport_h,
                                  ScreenRect rect, const ScenePickParams& params,
                                  std::vector<NodeId>& out_ids) {
    out_ids.clear();
    rect.normalize();
    if (rect.max_x - rect.min_x < 1.0f && rect.max_y - rect.min_y < 1.0f) {
        return;
    }

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

        ScreenRect node_rect{};
        if (!projectWorldAabbToScreen(view_proj, aabb_min, aabb_max, viewport_w, viewport_h,
                                      node_rect)) {
            continue;
        }
        if (rect.intersects(node_rect)) {
            out_ids.push_back(node.id);
        }
    }
}

} // namespace genesis
