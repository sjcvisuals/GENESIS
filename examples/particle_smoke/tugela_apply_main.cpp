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

    // --- 4) Spawn distribution: Surface vs Volume uniform change birth positions ---
    {
        auto make_ball = [](ParticleSpawnDistribution dist) {
            ParticleEmitter ball = makeGentleCirclesEmitter();
            ball.spawn_shape = ParticleSpawnShape::Sphere;
            ball.spawn_radius = 1.0f;
            ball.spawn_distribution = dist;
            ball.rate_per_second = 200.0f;
            ball.speed_min = 0.0f;
            ball.speed_max = 0.0f;
            ball.force_gravity = false;
            ball.gravity = glm::vec3(0.0f);
            return ball;
        };

        Scene surface_scene;
        surface_scene.createParticleEmitterNode("Ball", std::nullopt,
                                                make_ball(ParticleSpawnDistribution::Surface));
        surface_scene.updateWorldTransforms();
        ParticleWorld surface_world;
        surface_world.setSceneSeed(11);
        surface_world.simulate(surface_scene, 40, 0.01f);
        ok &= expect(surface_world.stats().alive > 0, "surface sphere should spawn");
        const std::uint64_t surface_hash = surface_world.stateHash();

        Scene volume_scene;
        volume_scene.createParticleEmitterNode("Ball", std::nullopt,
                                               make_ball(ParticleSpawnDistribution::VolumeUniform));
        volume_scene.updateWorldTransforms();
        ParticleWorld volume_world;
        volume_world.setSceneSeed(11);
        volume_world.simulate(volume_scene, 40, 0.01f);
        ok &= expect(volume_world.stats().alive > 0, "volume-uniform sphere should spawn");
        ok &= expect(volume_world.stateHash() != surface_hash,
                     "distribution mode must change spawn positions");

        // Live authoring: flip Surface → Volume uniform on the same emitter, clear, respawn.
        Scene live_scene;
        const NodeId id = live_scene.createParticleEmitterNode(
            "Ball", std::nullopt, make_ball(ParticleSpawnDistribution::Surface));
        live_scene.updateWorldTransforms();
        ParticleWorld live_world;
        live_world.setSceneSeed(11);
        live_world.simulate(live_scene, 40, 0.01f);
        ok &= expect(live_world.stateHash() == surface_hash, "same seed/mode → same hash");
        Node* node = live_scene.find(id);
        ok &= expect(node && node->particle_emitter.has_value(), "live ball node");
        node->particle_emitter->spawn_distribution = ParticleSpawnDistribution::VolumeUniform;
        live_world.clear();
        live_world.simulate(live_scene, 40, 0.01f);
        ok &= expect(live_world.stateHash() == volume_world.stateHash(),
                     "live distribution edit must affect new spawns");
    }

    if (!ok) {
        return 1;
    }
    std::printf("tugela_apply_smoke: ok\n");
    return 0;
}
