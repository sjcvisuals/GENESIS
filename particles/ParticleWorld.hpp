#pragma once

#include "particles/ParticleEmitter.hpp"
#include "renderer/MeshInstance.hpp"
#include "scene/Scene.hpp"

#include <cstdint>
#include <glm/vec3.hpp>
#include <unordered_map>
#include <vector>

namespace genesis {

struct ParticleSimStats {
    std::uint32_t alive = 0;
    std::uint32_t emitters = 0;
    std::uint32_t steps = 0;
};

struct ParticleViewBasis {
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    bool valid = false;
};

// Deterministic CPU particle world (Niagara-like modules, GPU-instanced draw).
class ParticleWorld {
public:
    void setSceneSeed(std::uint64_t seed) { scene_seed_ = seed; }
    std::uint64_t sceneSeed() const { return scene_seed_; }

    void clear();
    void reset();

    void simulate(Scene& scene, std::uint64_t steps, float dt);

    std::uint32_t emitBurstAtWorld(Scene& scene, NodeId emitter_node, const glm::vec3& world_position,
                                   std::uint32_t count);

    void rebuildToTime(Scene& scene, double production_time_seconds, float fixed_dt);

    ParticleSimStats stats() const { return stats_; }

    struct ParticleInstanceBatch {
        ParticleRenderMesh mesh = ParticleRenderMesh::Sprite;
        bool unlit = true;
        std::vector<GpuMeshInstance> instances;
    };
    void collectInstances(const Scene& scene, std::vector<ParticleInstanceBatch>& out,
                          const ParticleViewBasis& view = {}) const;

    void setFluidWorld(class FluidWorld* world) { fluid_world_ = world; }

    std::uint64_t stateHash() const;

private:
    struct EmitterRuntime {
        NodeId node_id = 0;
        double age_seconds = 0.0;
        double emit_accumulator = 0.0;
        std::uint64_t next_emit_index = 0;
        bool burst_fired = false;
        std::uint8_t timed_bursts_fired = 0; // bit per bursts[] entry
        bool finished = false;
        std::uint32_t alive_count = 0;
        glm::vec3 last_world_pos{0.0f};
        glm::vec3 velocity{0.0f};
        bool has_last_pos = false;
        double distance_spawn_accumulator = 0.0; // meters × spawn_per_unit
    };

    struct Particle {
        NodeId emitter_node = 0;
        std::uint64_t emit_index = 0;
        float age = 0.0f;
        float lifetime = 1.0f;
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float size0 = 0.1f;
        float sprite_aspect = 1.0f;
        ParticleRenderMesh mesh = ParticleRenderMesh::Sprite;
        bool unlit = true;
        bool world_space = true;
        bool alive = false;
        // Cached curve endpoints (evaluated from emitter curves at birth for determinism
        // when emitter curves change mid-flight; over-life still uses stored keys snapshot).
        ParticleCurve1 size_curve{};
        ParticleCurveColor color_curve{};
    };

    void syncEmitters(Scene& scene);
    void stepOnce(Scene& scene, float dt);
    void spawnFromEmitter(Scene& scene, EmitterRuntime& runtime, const ParticleEmitter& desc,
                          const Node& node, std::uint32_t count,
                          const glm::vec3* origin_override = nullptr);
    void recountAlive();
    std::size_t allocateParticle();
    EmitterRuntime* findEmitterRuntime(NodeId id);

    std::uint64_t scene_seed_ = 1;
    std::uint64_t global_step_ = 0;
    double sim_time_seconds_ = 0.0;
    std::vector<EmitterRuntime> emitters_;
    std::vector<Particle> particles_;
    std::vector<std::size_t> free_list_;
    ParticleSimStats stats_{};
    class FluidWorld* fluid_world_ = nullptr;
};

} // namespace genesis
