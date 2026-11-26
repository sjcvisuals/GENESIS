// Smoke: Tugela inspector→runtime apply path (trail live-edit, module stack, orphan cull).
#include "particles/ParticleEmitter.hpp"
#include "particles/ParticleWorld.hpp"
#include "scene/Scene.hpp"

#include <cstdio>

namespace {

bool expect(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "tugela_apply_smoke FAIL: %s\n", msg);
    }
    return cond;
}

} // namespace

int main() {
    using namespace genesis;
    bool ok = true;

    // --- 1) Trail live-apply: width change reaches living particles without reset ---
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
        node->particle_emitter->trail_width = 0.22f;
        node->particle_emitter->trail_alpha = 0.33f;
        node->particle_emitter->trail_segments = 6;
        world.applyAuthoringToLiving(scene);

        // Collect ribbon instances — width is encoded in instance scale; just ensure trails still draw
        // and a second simulate with disabled trails clears them.
        std::vector<ParticleWorld::ParticleInstanceBatch> batches;
        ParticleViewBasis view{};
        view.right = glm::vec3(1.0f, 0.0f, 0.0f);
        view.up = glm::vec3(0.0f, 1.0f, 0.0f);
        view.valid = true;
        world.collectInstances(scene, batches, view);
        std::size_t ribbons = 0;
        for (const auto& b : batches) {
            if (b.ribbon) {
                ribbons += b.instances.size();
            }
        }
        ok &= expect(ribbons > 0, "expected ribbon instances after trail live-apply");

        setOptionalModuleArmed(*node->particle_emitter, kStackTrail, false);
        world.applyAuthoringToLiving(scene);
        world.collectInstances(scene, batches, view);
        ribbons = 0;
        for (const auto& b : batches) {
            if (b.ribbon) {
                ribbons += b.instances.size();
            }
        }
        ok &= expect(ribbons == 0, "disabling trail must clear living ribbons");
        ok &= expect(particleStackHas(node->particle_emitter->module_stack, kStackTrail),
                     "disable must keep Trail on module_stack");
        ok &= expect(!particleStackHas(node->particle_emitter->module_enabled, kStackTrail),
                     "disable must clear module_enabled Trail bit");
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
        // Remaining particles should only belong to A (orphan B particles culled).
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

    // --- 4) Duplicate emitter deep-copies modules/params with unique sibling name ---
    {
        Scene scene;
        const NodeId system = scene.createParticleSystemNode("System", std::nullopt);
        ParticleEmitter src = makeGentleCirclesEmitter();
        src.rate_per_second = 22.0f;
        src.trail_enabled = true;
        src.trail_width = 0.41f;
        particleStackSet(src.module_stack, kStackTrail, true);
        particleStackSet(src.module_enabled, kStackTrail, true);
        particleStackSet(src.module_stack, kStackNoise, true);
        particleStackSet(src.module_enabled, kStackNoise, false); // present but muted
        syncOptionalStackFlags(src);
        const NodeId a = scene.createParticleEmitterNode("Emitter", system, src);
        if (Node* na = scene.find(a)) {
            na->local.position = glm::vec3(1.5f, 0.0f, -0.25f);
        }

        const NodeId b = scene.duplicateParticleEmitter(a);
        ok &= expect(b != 0 && b != a, "duplicate must create a new emitter id");
        const Node* nb = scene.find(b);
        ok &= expect(nb && nb->particle_emitter.has_value(), "duplicate node has emitter");
        ok &= expect(nb && nb->parent.has_value() && *nb->parent == system,
                     "duplicate stays under same System");
        ok &= expect(nb && nb->name == "Emitter_2", "first duplicate names Emitter_2");
        ok &= expect(nb && nb->local.position.x == 1.5f, "duplicate copies local transform");
        ok &= expect(nb && nb->particle_emitter->rate_per_second == 22.0f,
                     "duplicate copies rate");
        ok &= expect(nb && nb->particle_emitter->trail_width == 0.41f,
                     "duplicate copies trail params");
        ok &= expect(nb && particleStackHas(nb->particle_emitter->module_stack, kStackTrail),
                     "duplicate keeps Trail on stack");
        ok &= expect(nb && particleStackHas(nb->particle_emitter->module_stack, kStackNoise) &&
                         !particleStackHas(nb->particle_emitter->module_enabled, kStackNoise),
                     "duplicate preserves muted Noise on stack");

        // Independent authoring: mutate source must not affect clone.
        if (Node* na = scene.find(a); na && na->particle_emitter.has_value()) {
            na->particle_emitter->rate_per_second = 99.0f;
        }
        ok &= expect(nb && nb->particle_emitter->rate_per_second == 22.0f,
                     "duplicate is a deep copy (independent params)");

        const NodeId c = scene.duplicateParticleEmitter(b);
        const Node* nc = scene.find(c);
        ok &= expect(nc && nc->name == "Emitter_3", "second duplicate names Emitter_3");
    }

    if (!ok) {
        return 1;
    }
    std::printf("tugela_apply_smoke: ok\n");
    return 0;
}
