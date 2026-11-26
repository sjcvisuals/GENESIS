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

    // --- 5) Spawn distribution: Surface vs Volume uniform change birth positions ---
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

    // --- 6) Sprite off + no Mesh/Trail → live-apply must not spawn Sphere balls ---
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
