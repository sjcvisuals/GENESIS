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

    // --- 4) Sprite off + no Mesh/Trail → live-apply must not spawn Sphere balls ---
    {
        Scene scene;
        ParticleEmitter soft = makeGentleSoftEmitter();
        const NodeId id = scene.createParticleEmitterNode("Invisible", std::nullopt, soft);
        scene.updateWorldTransforms();

        ParticleWorld world;
        world.setSceneSeed(11);
        world.simulate(scene, 40, 0.01f);
        ok &= expect(world.stats().alive > 0, "expected living particles before renderer mute");

        Node* node = scene.find(id);
        ok &= expect(node && node->particle_emitter.has_value(), "invisible emitter node");
        node->particle_emitter->mod_sprite = false;
        node->particle_emitter->mod_mesh = false;
        setOptionalModuleArmed(*node->particle_emitter, kStackTrail, false);
        world.applyAuthoringToLiving(scene);

        ParticleViewBasis view{};
        view.right = glm::vec3(1.0f, 0.0f, 0.0f);
        view.up = glm::vec3(0.0f, 1.0f, 0.0f);
        view.valid = true;
        std::vector<ParticleWorld::ParticleInstanceBatch> batches;
        world.collectInstances(scene, batches, view);
        std::size_t drawn = 0;
        std::size_t spheres = 0;
        for (const auto& b : batches) {
            drawn += b.instances.size();
            if (b.mesh == ParticleRenderMesh::Sphere || b.mesh == ParticleRenderMesh::Cube) {
                spheres += b.instances.size();
            }
        }
        ok &= expect(drawn == 0, "all renderers off must draw nothing (live-apply)");
        ok &= expect(spheres == 0, "must not fall back to Sphere/Cube heads");

        // Mesh Renderer alone → Sphere heads visible again.
        node->particle_emitter->mod_mesh = true;
        node->particle_emitter->render_mesh = ParticleRenderMesh::Sphere;
        world.applyAuthoringToLiving(scene);
        world.collectInstances(scene, batches, view);
        spheres = 0;
        for (const auto& b : batches) {
            if (b.mesh == ParticleRenderMesh::Sphere) {
                spheres += b.instances.size();
            }
        }
        ok &= expect(spheres > 0, "Mesh Renderer on must draw Sphere heads");
    }

    if (!ok) {
        return 1;
    }
    std::printf("tugela_apply_smoke: ok\n");
    return 0;
}
