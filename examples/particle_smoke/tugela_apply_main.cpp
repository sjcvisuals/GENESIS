// Smoke: Tugela inspector→runtime apply path (trail live-edit, module stack, orphan cull).
#include "particles/ParticleEmitter.hpp"
#include "particles/ParticleWorld.hpp"
#include "renderer/MeshInstance.hpp"
#include "scene/Scene.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace {

bool expect(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "tugela_apply_smoke FAIL: %s\n", msg);
    }
    return cond;
}

float ribbonWidth(const genesis::GpuMeshInstance& inst) {
    return glm::length(glm::vec3(inst.model[0]));
}

float ribbonLength(const genesis::GpuMeshInstance& inst) {
    return glm::length(glm::vec3(inst.model[1]));
}

} // namespace

int main() {
    using namespace genesis;
    bool ok = true;

    // --- 1) Trail live-apply: width/alpha/segments reach living draw ---
    {
        Scene scene;
        ParticleEmitter trail = makeGentleSoftEmitter();
        const NodeId id = scene.createParticleEmitterNode("Trail", std::nullopt, trail);
        scene.updateWorldTransforms();

        ParticleWorld world;
        world.setSceneSeed(7);
        world.simulate(scene, 40, 0.01f);
        ok &= expect(world.stats().alive > 0, "expected living particles before trail edit");

        Node* node = scene.find(id);
        ok &= expect(node && node->particle_emitter.has_value(), "trail emitter node");

        std::vector<ParticleWorld::ParticleInstanceBatch> batches;
        ParticleViewBasis view{};
        view.right = glm::vec3(1.0f, 0.0f, 0.0f);
        view.up = glm::vec3(0.0f, 1.0f, 0.0f);
        view.valid = true;

        auto maxRibbonMetric = [&](auto metric) {
            float v = 0.0f;
            for (const auto& b : batches) {
                if (!b.ribbon) {
                    continue;
                }
                for (const auto& inst : b.instances) {
                    v = std::max(v, metric(inst));
                }
            }
            return v;
        };
        auto ribbonCount = [&]() {
            std::size_t n = 0;
            for (const auto& b : batches) {
                if (b.ribbon) {
                    n += b.instances.size();
                }
            }
            return n;
        };

        // Wide vs thin Width (head) must change ribbon instance scale (was look-locked).
        node->particle_emitter->trail_width = 0.95f;
        node->particle_emitter->trail_alpha = 0.9f;
        world.applyAuthoringToLiving(scene);
        world.simulate(scene, 5, 0.01f);
        world.collectInstances(scene, batches, view);
        const float wide_w = maxRibbonMetric(ribbonWidth);
        ok &= expect(ribbonCount() > 0, "expected ribbons at wide width");

        node->particle_emitter->trail_width = 0.15f;
        world.applyAuthoringToLiving(scene);
        world.simulate(scene, 5, 0.01f);
        world.collectInstances(scene, batches, view);
        const float thin_w = maxRibbonMetric(ribbonWidth);
        ok &= expect(wide_w > thin_w * 1.5f,
                     "Width (head) must change ribbon instance scale (was look-locked)");

        // Alpha 0 must hide ribbons without removing the module from the stack.
        node->particle_emitter->trail_width = 0.75f;
        node->particle_emitter->trail_alpha = 0.0f;
        world.applyAuthoringToLiving(scene);
        world.collectInstances(scene, batches, view);
        ok &= expect(ribbonCount() == 0, "trail alpha 0 must hide living ribbons");

        node->particle_emitter->trail_alpha = 0.9f;
        setOptionalModuleArmed(*node->particle_emitter, kStackTrail, false);
        world.applyAuthoringToLiving(scene);
        world.collectInstances(scene, batches, view);
        ok &= expect(ribbonCount() == 0, "disabling trail must clear living ribbons");
        ok &= expect(particleStackHas(node->particle_emitter->module_stack, kStackTrail),
                     "disable must keep Trail on module_stack");
        ok &= expect(!particleStackHas(node->particle_emitter->module_enabled, kStackTrail),
                     "disable must clear module_enabled Trail bit");

        // Re-enable must restore drawing after history reseeds.
        setOptionalModuleArmed(*node->particle_emitter, kStackTrail, true);
        world.applyAuthoringToLiving(scene);
        world.simulate(scene, 20, 0.01f);
        world.collectInstances(scene, batches, view);
        ok &= expect(ribbonCount() > 0, "re-enable trail must draw ribbons again");

        // Segments / sample / lifetime must change ribbon length (was capped to ~4.5×head).
        node->particle_emitter->trail_segments = 20;
        node->particle_emitter->trail_sample_distance = 0.08f;
        node->particle_emitter->trail_lifetime = 8.0f;
        world.applyAuthoringToLiving(scene);
        world.simulate(scene, 50, 0.02f);
        world.collectInstances(scene, batches, view);
        const float long_len = maxRibbonMetric(ribbonLength);

        node->particle_emitter->trail_segments = 3;
        node->particle_emitter->trail_sample_distance = 0.02f;
        node->particle_emitter->trail_lifetime = 0.15f;
        world.applyAuthoringToLiving(scene);
        world.simulate(scene, 50, 0.02f);
        world.collectInstances(scene, batches, view);
        const float short_len = maxRibbonMetric(ribbonLength);
        ok &= expect(long_len > short_len * 1.2f,
                     "segments/sample/lifetime must change ribbon length");
        (void)id;
    }

    // --- 2) Remove emitter culls orphans ---
    {
        Scene scene;
        const NodeId system = scene.createEmptyNode("System", std::nullopt);
        const NodeId a =
            scene.createParticleEmitterNode("A", system, makeGentleCirclesEmitter());
        const NodeId b = scene.createParticleEmitterNode("B", system, makeGentleSoftEmitter());
        scene.updateWorldTransforms();

        ParticleWorld world;
        world.setSceneSeed(3);
        world.simulate(scene, 30, 0.01f);
        const std::uint32_t alive_before = world.stats().alive;
        ok &= expect(alive_before > 0, "expected particles before delete");
        ok &= expect(world.stats().emitters == 2, "expected two emitter runtimes");

        scene.deleteNode(b);
        world.applyAuthoringToLiving(scene);
        ok &= expect(world.stats().emitters == 1, "delete emitter must drop runtime");
        ok &= expect(world.stats().alive > 0, "emitter A particles should remain");
        (void)a;
    }

    // --- 3) Burst mute keeps authored counts ---
    {
        ParticleEmitter e = makeGentleCirclesEmitter();
        e.burst_count = 48;
        particleStackSet(e.module_stack, kStackBurst, true);
        particleStackSet(e.module_enabled, kStackBurst, true);
        ok &= expect(optionalBurstArmed(e), "burst armed");
        particleStackSet(e.module_enabled, kStackBurst, false);
        syncOptionalStackFlags(e);
        ok &= expect(!optionalBurstArmed(e), "burst muted");
        ok &= expect(e.burst_count == 48, "mute must preserve burst_count");
        ok &= expect(particleStackHas(e.module_stack, kStackBurst), "mute keeps burst on stack");
    }

    if (!ok) {
        return 1;
    }
    std::printf("tugela_apply_smoke: ok\n");
    return 0;
}
