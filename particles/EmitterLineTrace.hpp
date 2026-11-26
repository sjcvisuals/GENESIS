#pragma once

// Tugela emitter Line Trace positioning — place an emitter along an authored ray,
// optionally snapping to the nearest mesh with Collision on (Node::collision_enabled).

#include "particles/ParticleEmitter.hpp"
#include "scene/Scene.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace genesis {

struct EmitterLineTraceDebugSample {
    NodeId emitter_id = 0;
    glm::vec3 origin{0.0f};
    glm::vec3 end{0.0f};
    glm::vec3 position{0.0f};
    bool used_collision_hit = false;
    bool active = false;
};

struct EmitterLineTraceEval {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, 1.0f}; // unit
    float ray_length = 1.0f;               // meters along direction (segment / max)
    glm::vec3 position{0.0f};              // world placement
    bool used_collision_hit = false;
    bool ok = false;
};

// Collision candidate for Line Trace snap (built from mesh actors + local AABB).
struct EmitterLineTraceCollider {
    NodeId node_id = 0;
    glm::mat4 world{1.0f};
    glm::vec3 local_aabb_min{-0.5f};
    glm::vec3 local_aabb_max{0.5f};
};

inline bool emitterLineTraceRay(const ParticleEmitter& e, glm::vec3& out_origin,
                                glm::vec3& out_dir, float& out_length) {
    if (e.line_trace_use_endpoints) {
        out_origin = e.line_trace_from;
        glm::vec3 delta = e.line_trace_to - e.line_trace_from;
        const float len = glm::length(delta);
        if (len < 1e-5f) {
            out_dir = glm::vec3(0.0f, 0.0f, 1.0f);
            out_length = 0.0f;
            return false;
        }
        out_dir = delta / len;
        out_length = len;
        return true;
    }

    out_origin = e.line_trace_origin;
    const float dlen = glm::length(e.line_trace_direction);
    if (dlen < 1e-5f) {
        out_dir = glm::vec3(0.0f, 0.0f, 1.0f);
        out_length = std::max(0.0f, e.line_trace_max_distance);
        return false;
    }
    out_dir = e.line_trace_direction / dlen;
    out_length = std::max(0.0f, e.line_trace_max_distance);
    return true;
}

inline float emitterLineTraceParamDistance(const ParticleEmitter& e, float ray_length) {
    if (e.line_trace_distance_normalized) {
        const float t = std::clamp(e.line_trace_distance, 0.0f, 1.0f);
        return t * std::max(0.0f, ray_length);
    }
    return std::clamp(e.line_trace_distance, 0.0f, std::max(0.0f, ray_length));
}

namespace detail {

inline void worldAabbFromLocal(const glm::mat4& world, const glm::vec3& local_min,
                               const glm::vec3& local_max, glm::vec3& out_min,
                               glm::vec3& out_max) {
    const std::array<glm::vec3, 8> corners = {
        glm::vec3(local_min.x, local_min.y, local_min.z),
        glm::vec3(local_max.x, local_min.y, local_min.z),
        glm::vec3(local_min.x, local_max.y, local_min.z),
        glm::vec3(local_max.x, local_max.y, local_min.z),
        glm::vec3(local_min.x, local_min.y, local_max.z),
        glm::vec3(local_max.x, local_min.y, local_max.z),
        glm::vec3(local_min.x, local_max.y, local_max.z),
        glm::vec3(local_max.x, local_max.y, local_max.z),
    };
    out_min = glm::vec3(world * glm::vec4(corners[0], 1.0f));
    out_max = out_min;
    for (std::size_t i = 1; i < corners.size(); ++i) {
        const glm::vec3 p = glm::vec3(world * glm::vec4(corners[i], 1.0f));
        out_min = glm::min(out_min, p);
        out_max = glm::max(out_max, p);
    }
}

inline void expandThinAabb(glm::vec3& aabb_min, glm::vec3& aabb_max, float min_extent,
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

inline bool rayIntersectsAabb(const glm::vec3& origin, const glm::vec3& dir,
                              const glm::vec3& aabb_min, const glm::vec3& aabb_max,
                              float& out_distance) {
    float t_min = 0.0f;
    float t_max = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(dir[axis]) < 1e-8f) {
            if (origin[axis] < aabb_min[axis] || origin[axis] > aabb_max[axis]) {
                return false;
            }
            continue;
        }
        const float inv_d = 1.0f / dir[axis];
        float t0 = (aabb_min[axis] - origin[axis]) * inv_d;
        float t1 = (aabb_max[axis] - origin[axis]) * inv_d;
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

} // namespace detail

// Closest collider AABB hit. Skips skip_node. Caller only passes Collision-on meshes
// (Hidden in Game / Visible do not gate — matches wall-ray).
inline std::optional<float> closestLineTraceHitDistance(
    const std::vector<EmitterLineTraceCollider>& colliders, const glm::vec3& origin,
    const glm::vec3& dir, float max_t, NodeId skip_node) {
    std::optional<float> best;
    for (const EmitterLineTraceCollider& c : colliders) {
        if (c.node_id == skip_node) {
            continue;
        }
        glm::vec3 aabb_min{};
        glm::vec3 aabb_max{};
        detail::worldAabbFromLocal(c.world, c.local_aabb_min, c.local_aabb_max, aabb_min,
                                   aabb_max);
        detail::expandThinAabb(aabb_min, aabb_max, 0.01f, 0.002f);
        float distance = 0.0f;
        if (!detail::rayIntersectsAabb(origin, dir, aabb_min, aabb_max, distance)) {
            continue;
        }
        if (distance < 1e-4f || distance > max_t) {
            continue;
        }
        if (!best.has_value() || distance < *best) {
            best = distance;
        }
    }
    return best;
}

inline EmitterLineTraceEval evaluateEmitterLineTrace(
    const ParticleEmitter& e, const std::vector<EmitterLineTraceCollider>& colliders,
    NodeId emitter_id) {
    EmitterLineTraceEval eval{};
    if (!e.line_trace_enabled) {
        return eval;
    }

    float ray_length = 0.0f;
    const bool ray_ok = emitterLineTraceRay(e, eval.origin, eval.direction, ray_length);
    eval.ray_length = ray_length;
    const float fallback_t = emitterLineTraceParamDistance(e, ray_length);
    eval.position = eval.origin + eval.direction * fallback_t;
    eval.ok = ray_ok || ray_length > 0.0f;

    if (e.line_trace_snap_to_collision && ray_length > 1e-5f) {
        if (const std::optional<float> hit = closestLineTraceHitDistance(
                colliders, eval.origin, eval.direction, ray_length, emitter_id)) {
            eval.position = eval.origin + eval.direction * (*hit);
            eval.used_collision_hit = true;
            eval.ok = true;
        }
    }
    return eval;
}

inline void setNodeWorldPositionLocal(Scene& scene, Node& node, const glm::vec3& world_pos) {
    if (node.parent.has_value()) {
        // Avoid Scene::find (compiled) so this header stays smoke-testable without Vulkan.
        for (const Node& candidate : scene.nodes) {
            if (candidate.id != *node.parent) {
                continue;
            }
            const glm::mat4 inv = glm::inverse(candidate.world);
            node.local.position = glm::vec3(inv * glm::vec4(world_pos, 1.0f));
            return;
        }
    }
    node.local.position = world_pos;
}

// Apply Line Trace to every enabled emitter. Worlds for parents/colliders must be current;
// caller should refresh worlds afterward. Returns true if any local position was written.
inline bool applyEmitterLineTraces(Scene& scene,
                                   const std::vector<EmitterLineTraceCollider>& colliders,
                                   std::vector<EmitterLineTraceDebugSample>* debug_out = nullptr) {
    if (debug_out) {
        debug_out->clear();
    }
    bool any = false;
    for (Node& node : scene.nodes) {
        if (!node.particle_emitter.has_value()) {
            continue;
        }
        ParticleEmitter& e = *node.particle_emitter;
        if (!e.line_trace_enabled) {
            continue;
        }
        const EmitterLineTraceEval eval =
            evaluateEmitterLineTrace(e, colliders, node.id);
        setNodeWorldPositionLocal(scene, node, eval.position);
        any = true;

        if (debug_out && e.line_trace_debug_draw) {
            EmitterLineTraceDebugSample sample{};
            sample.emitter_id = node.id;
            sample.origin = eval.origin;
            sample.end = eval.origin + eval.direction * eval.ray_length;
            sample.position = eval.position;
            sample.used_collision_hit = eval.used_collision_hit;
            sample.active = true;
            debug_out->push_back(sample);
        }
    }
    return any;
}

// Collect Collision-on mesh actors into Line Trace colliders.
// get_local_aabb(node, out_min, out_max) returns false to skip.
template <typename GetLocalAabb>
inline void collectEmitterLineTraceColliders(const Scene& scene, GetLocalAabb&& get_local_aabb,
                                             std::vector<EmitterLineTraceCollider>& out) {
    out.clear();
    for (const Node& node : scene.nodes) {
        if (!node.has_mesh || !node.collision_enabled) {
            continue;
        }
        EmitterLineTraceCollider c{};
        c.node_id = node.id;
        c.world = node.world;
        if (!get_local_aabb(node, c.local_aabb_min, c.local_aabb_max)) {
            continue;
        }
        out.push_back(c);
    }
}

} // namespace genesis
