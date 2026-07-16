#pragma once

#include "particles/ParticleEmitter.hpp"
#include "renderer/MeshInstance.hpp"
#include "scene/Scene.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace genesis {

struct ParticleSimStats {
    std::uint32_t alive = 0;
    std::uint32_t emitters = 0;
    std::uint32_t steps = 0;
};

// CPU SoA particle world. Deterministic given scene_seed + fixed steps + emitter params.
class ParticleWorld {
public:
    void setSceneSeed(std::uint64_t seed) { scene_seed_ = seed; }
    std::uint64_t sceneSeed() const { return scene_seed_; }

    void clear();
    void reset(); // clear particles + accumulators; keep seed

    // Fixed-step simulation. Call `steps` times with `dt` (usually ProductionClock fixed step).
    void simulate(Scene& scene, std::uint64_t steps, float dt);

    // Seek/rebuild helper: clear and fast-forward absolute production time.
    void rebuildToTime(Scene& scene, double production_time_seconds, float fixed_dt);

    ParticleSimStats stats() const { return stats_; }

    // Append mesh instances for alive particles (uses emitter.render_mesh hint via out tags).
    struct ParticleInstanceBatch {
        ParticleRenderMesh mesh = ParticleRenderMesh::Cube;
        bool unlit = true;
        std::vector<GpuMeshInstance> instances;
    };
    void collectInstances(const Scene& scene, std::vector<ParticleInstanceBatch>& out) const;

    std::uint64_t stateHash() const;

private:
    struct EmitterRuntime {
        NodeId node_id = 0;
        double age_seconds = 0.0;
        double emit_accumulator = 0.0;
        std::uint64_t next_emit_index = 0;
        bool burst_fired = false;
        bool finished = false;
    };

    struct Particle {
        NodeId emitter_node = 0;
        std::uint64_t emit_index = 0;
        float age = 0.0f;
        float lifetime = 1.0f;
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float size0 = 0.1f;
        glm::vec4 color0{1.0f};
        glm::vec4 color1{1.0f};
        float size_end_mul = 0.25f;
        ParticleRenderMesh mesh = ParticleRenderMesh::Cube;
        bool unlit = true;
        bool world_space = true;
        bool alive = false;
    };

    void syncEmitters(Scene& scene);
    void stepOnce(Scene& scene, float dt);
    void spawnFromEmitter(Scene& scene, EmitterRuntime& runtime, const ParticleEmitter& desc,
                          const Node& node, std::uint32_t count);
    std::size_t allocateParticle();

    std::uint64_t scene_seed_ = 1;
    std::uint64_t global_step_ = 0;
    double sim_time_seconds_ = 0.0;
    std::vector<EmitterRuntime> emitters_;
    std::vector<Particle> particles_;
    std::vector<std::size_t> free_list_;
    ParticleSimStats stats_{};
};

} // namespace genesis
