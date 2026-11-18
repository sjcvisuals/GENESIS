// Header-only smoke for Tugela particle authoring types (no Vulkan).
#include "particles/ParticleCurve.hpp"
#include "particles/ParticleEmitter.hpp"

#include <cmath>
#include <cstdio>

namespace {

bool near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    using genesis::ParticleCurve1;
    using genesis::ParticleCurveColor;
    using genesis::ParticleRenderMesh;

    const ParticleCurve1 size = ParticleCurve1::linear(1.0f, 0.25f);
    if (!near(size.evaluate(0.0f), 1.0f) || !near(size.evaluate(1.0f), 0.25f) ||
        !near(size.evaluate(0.5f), 0.625f)) {
        std::fprintf(stderr, "particle_smoke: size curve evaluate failed\n");
        return 1;
    }

    ParticleCurveColor color{};
    color.count = 3;
    color.keys[0] = {0.0f, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)};
    color.keys[1] = {0.2f, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)};
    color.keys[2] = {1.0f, glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)};
    const glm::vec4 mid = color.evaluate(0.2f);
    if (!near(mid.a, 1.0f) || !near(mid.r, 1.0f)) {
        std::fprintf(stderr, "particle_smoke: color curve key failed\n");
        return 1;
    }

    genesis::ParticleEmitter soft = genesis::makeGentleSoftEmitter();
    if (soft.render_mesh != ParticleRenderMesh::Sprite || soft.size_over_life.count < 2 ||
        soft.color_over_life.count < 2 || !soft.trail_enabled || soft.trail_segments < 2) {
        std::fprintf(stderr, "particle_smoke: gentle soft preset missing Tugela modules/trails\n");
        return 1;
    }

    genesis::ParticleEmitter tracker = genesis::makeOscTrackerBubbleEmitter();
    // Noise stays off by default — soft additive + noise reads as firefly sparkle.
    if (tracker.render_mesh != ParticleRenderMesh::Sprite || tracker.force_noise ||
        !tracker.trail_enabled || tracker.trail_width_end > tracker.trail_width) {
        std::fprintf(stderr, "particle_smoke: OSC tracker preset incomplete\n");
        return 1;
    }

    genesis::rebuildEmitterCurvesFromEndpoints(soft);
    if (soft.size_over_life.count != 2 || soft.color_over_life.count != 2) {
        std::fprintf(stderr, "particle_smoke: rebuild curves failed\n");
        return 1;
    }

    std::printf("particle_smoke: ok (sprite+curves+trail modules)\n");
    return 0;
}
