// Header-only smoke: Tugela emitter Line Trace placement + Collision gate (no Vulkan).
#include "particles/EmitterLineTrace.hpp"
#include "particles/ParticleEmitter.hpp"
#include "scene/Scene.hpp"

#include <cstdio>
#include <vector>

namespace {

bool expect(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "line_trace_smoke FAIL: %s\n", msg);
    }
    return cond;
}

bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f) {
    return glm::length(a - b) <= eps;
}

} // namespace

int main() {
    using namespace genesis;
    bool ok = true;

    // --- 1) Distance along origin→direction (normalized) ---
    {
        ParticleEmitter e = makeDefaultParticleEmitter();
        e.line_trace_enabled = true;
        e.line_trace_use_endpoints = false;
        e.line_trace_origin = glm::vec3(0.0f, 1.0f, 0.0f);
        e.line_trace_direction = glm::vec3(0.0f, 0.0f, 4.0f); // non-unit ok
        e.line_trace_max_distance = 10.0f;
        e.line_trace_distance_normalized = true;
        e.line_trace_distance = 0.5f;
        e.line_trace_snap_to_collision = false;

        const EmitterLineTraceEval eval =
            evaluateEmitterLineTrace(e, std::vector<EmitterLineTraceCollider>{}, 0);
        ok &= expect(eval.ok, "normalized distance eval ok");
        ok &= expect(near(eval.position, glm::vec3(0.0f, 1.0f, 5.0f)),
                     "0.5 along 10m +Z from y=1");
    }

    // --- 2) From→To meters distance ---
    {
        ParticleEmitter e = makeDefaultParticleEmitter();
        e.line_trace_enabled = true;
        e.line_trace_use_endpoints = true;
        e.line_trace_from = glm::vec3(0.0f);
        e.line_trace_to = glm::vec3(0.0f, 0.0f, 4.0f);
        e.line_trace_distance_normalized = false;
        e.line_trace_distance = 1.5f;

        const EmitterLineTraceEval eval =
            evaluateEmitterLineTrace(e, std::vector<EmitterLineTraceCollider>{}, 0);
        ok &= expect(near(eval.position, glm::vec3(0.0f, 0.0f, 1.5f)), "1.5m along from→to");
    }

    // --- 3) Snap honors collision_enabled (via collider list) ---
    {
        EmitterLineTraceCollider wall{};
        wall.node_id = 1;
        wall.world = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 3.0f));
        wall.local_aabb_min = glm::vec3(-0.5f);
        wall.local_aabb_max = glm::vec3(0.5f);
        std::vector<EmitterLineTraceCollider> on = {wall};

        ParticleEmitter e = makeDefaultParticleEmitter();
        e.line_trace_enabled = true;
        e.line_trace_origin = glm::vec3(0.0f);
        e.line_trace_direction = glm::vec3(0.0f, 0.0f, 1.0f);
        e.line_trace_max_distance = 20.0f;
        e.line_trace_distance_normalized = true;
        e.line_trace_distance = 1.0f;
        e.line_trace_snap_to_collision = true;

        EmitterLineTraceEval hit = evaluateEmitterLineTrace(e, on, 99);
        ok &= expect(hit.used_collision_hit, "snap hits Collision-on mesh");
        ok &= expect(hit.position.z > 2.0f && hit.position.z < 4.0f, "hit near wall AABB");

        // Collision-off → empty collider list (SceneRuntime skips !collision_enabled).
        EmitterLineTraceEval miss =
            evaluateEmitterLineTrace(e, std::vector<EmitterLineTraceCollider>{}, 99);
        ok &= expect(!miss.used_collision_hit, "Collision-off skipped");
        ok &= expect(near(miss.position, glm::vec3(0.0f, 0.0f, 20.0f)),
                     "miss falls back to distance along ray");
    }

    // --- 4) collectEmitterLineTraceColliders gates on collision_enabled ---
    {
        Scene scene;
        Node wall{};
        wall.id = 1;
        wall.has_mesh = true;
        wall.collision_enabled = true;
        wall.world = glm::mat4(1.0f);
        scene.nodes.push_back(wall);

        std::vector<EmitterLineTraceCollider> colliders;
        collectEmitterLineTraceColliders(
            scene,
            [](const Node&, glm::vec3& mn, glm::vec3& mx) {
                mn = glm::vec3(-0.5f);
                mx = glm::vec3(0.5f);
                return true;
            },
            colliders);
        ok &= expect(colliders.size() == 1, "Collision-on collected");

        scene.nodes[0].collision_enabled = false;
        collectEmitterLineTraceColliders(
            scene,
            [](const Node&, glm::vec3& mn, glm::vec3& mx) {
                mn = glm::vec3(-0.5f);
                mx = glm::vec3(0.5f);
                return true;
            },
            colliders);
        ok &= expect(colliders.empty(), "Collision-off not collected");
    }

    // --- 5) applyEmitterLineTraces writes emitter local position ---
    {
        Scene scene;
        ParticleEmitter e = makeDefaultParticleEmitter();
        e.line_trace_enabled = true;
        e.line_trace_origin = glm::vec3(2.0f, 0.0f, 0.0f);
        e.line_trace_direction = glm::vec3(0.0f, 1.0f, 0.0f);
        e.line_trace_max_distance = 4.0f;
        e.line_trace_distance_normalized = true;
        e.line_trace_distance = 0.25f;
        e.line_trace_debug_draw = true;

        Node emitter{};
        emitter.id = 7;
        emitter.name = "LT";
        emitter.has_particle_emitter = true;
        emitter.particle_emitter = e;
        emitter.world = glm::mat4(1.0f);
        scene.nodes.push_back(emitter);

        std::vector<EmitterLineTraceDebugSample> dbg;
        ok &= expect(applyEmitterLineTraces(scene, {}, &dbg), "apply wrote positions");
        ok &= expect(near(scene.nodes[0].local.position, glm::vec3(2.0f, 1.0f, 0.0f)),
                     "emitter placed at 25% along ray");
        ok &= expect(dbg.size() == 1 && dbg[0].active, "debug sample recorded");
    }

    if (!ok) {
        return 1;
    }
    std::printf("line_trace_smoke: ok\n");
    return 0;
}
