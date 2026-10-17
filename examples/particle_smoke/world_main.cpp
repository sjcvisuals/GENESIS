// Deterministic ParticleWorld smoke (CPU sim + sprite batching; no window).
#include "particles/ParticleWorld.hpp"
#include "scene/Scene.hpp"

#include <cstdio>
#include <vector>

int main() {
    genesis::Scene scene;
    scene.createParticleEmitterNode("Ring", std::nullopt, genesis::makeBubbleRingEmitter());
    scene.createParticleEmitterNode("Tracker", std::nullopt, genesis::makeOscTrackerBubbleEmitter());
    scene.updateWorldTransforms();

    genesis::ParticleWorld world;
    world.setSceneSeed(42);
    world.simulate(scene, 120, 0.01f);
    const auto stats = world.stats();
    const std::uint64_t h1 = world.stateHash();
    world.rebuildToTime(scene, 1.2, 0.01f);
    const std::uint64_t h2 = world.stateHash();
    if (h1 != h2) {
        std::fprintf(stderr, "particle_world_smoke: determinism hash mismatch\n");
        return 1;
    }
    if (stats.alive == 0 || stats.emitters != 2) {
        std::fprintf(stderr, "particle_world_smoke: unexpected alive=%u emitters=%u\n", stats.alive,
                     stats.emitters);
        return 1;
    }

    std::vector<genesis::ParticleWorld::ParticleInstanceBatch> batches;
    genesis::ParticleViewBasis view{};
    view.right = glm::vec3(1.0f, 0.0f, 0.0f);
    view.up = glm::vec3(0.0f, 1.0f, 0.0f);
    view.valid = true;
    world.collectInstances(scene, batches, view);

    std::size_t sprites = 0;
    for (const auto& batch : batches) {
        if (batch.mesh == genesis::ParticleRenderMesh::Sprite) {
            sprites += batch.instances.size();
        }
    }
    if (sprites == 0) {
        std::fprintf(stderr, "particle_world_smoke: expected sprite instances\n");
        return 1;
    }

    std::printf("particle_world_smoke: ok alive=%u emitters=%u sprites=%zu hash=%llu\n", stats.alive,
                stats.emitters, sprites, static_cast<unsigned long long>(h1));
    return 0;
}
