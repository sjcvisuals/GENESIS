#include "particles/ParticleWorld.hpp"

#include "particles/FluidWorld.hpp"
#include "particles/ParticleRng.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace genesis {

namespace {

constexpr std::uint32_t kChanLifetime = 1;
constexpr std::uint32_t kChanSpeed = 2;
constexpr std::uint32_t kChanSize = 3;
constexpr std::uint32_t kChanDirX = 4;
constexpr std::uint32_t kChanDirY = 5;
constexpr std::uint32_t kChanDirZ = 6;
constexpr std::uint32_t kChanPosX = 7;
constexpr std::uint32_t kChanPosY = 8;
constexpr std::uint32_t kChanPosZ = 9;

glm::vec3 randomUnitVector(std::uint64_t seed, std::uint64_t emitter_id, std::uint64_t emit_index) {
    const float u = particleRandom01(seed, emitter_id, emit_index, kChanDirX);
    const float v = particleRandom01(seed, emitter_id, emit_index, kChanDirY);
    const float theta = u * glm::two_pi<float>();
    const float z = v * 2.0f - 1.0f;
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return glm::vec3(r * std::cos(theta), z, r * std::sin(theta));
}

glm::vec3 coneDirection(const glm::mat4& world, float angle_deg, std::uint64_t seed,
                        std::uint64_t emitter_id, std::uint64_t emit_index) {
    const float angle = glm::radians(std::max(angle_deg, 0.0f));
    const float u = particleRandom01(seed, emitter_id, emit_index, kChanDirX);
    const float v = particleRandom01(seed, emitter_id, emit_index, kChanDirY);
    const float cos_a = std::cos(angle);
    const float z = cos_a + (1.0f - cos_a) * u;
    const float sin_t = std::sqrt(std::max(0.0f, 1.0f - z * z));
    const float phi = v * glm::two_pi<float>();
    const glm::vec3 local(sin_t * std::cos(phi), z, sin_t * std::sin(phi));
    const glm::vec3 world_dir = glm::normalize(glm::mat3(world) * local);
    return world_dir;
}

} // namespace

void ParticleWorld::clear() {
    emitters_.clear();
    particles_.clear();
    free_list_.clear();
    global_step_ = 0;
    sim_time_seconds_ = 0.0;
    stats_ = {};
}

void ParticleWorld::reset() {
    particles_.clear();
    free_list_.clear();
    for (EmitterRuntime& emitter : emitters_) {
        emitter.age_seconds = 0.0;
        emitter.emit_accumulator = 0.0;
        emitter.next_emit_index = 0;
        emitter.burst_fired = false;
        emitter.finished = false;
    }
    global_step_ = 0;
    sim_time_seconds_ = 0.0;
    stats_ = {};
}

void ParticleWorld::syncEmitters(Scene& scene) {
    std::unordered_map<NodeId, std::size_t> existing;
    existing.reserve(emitters_.size());
    for (std::size_t i = 0; i < emitters_.size(); ++i) {
        existing[emitters_[i].node_id] = i;
    }

    std::vector<EmitterRuntime> next;
    next.reserve(emitters_.size());

    for (const Node& node : scene.nodes) {
        if (!node.particle_emitter.has_value()) {
            continue;
        }
        EmitterRuntime runtime{};
        runtime.node_id = node.id;
        if (const auto it = existing.find(node.id); it != existing.end()) {
            runtime = emitters_[it->second];
        }
        next.push_back(runtime);
    }
    emitters_ = std::move(next);
}

std::size_t ParticleWorld::allocateParticle() {
    if (!free_list_.empty()) {
        const std::size_t index = free_list_.back();
        free_list_.pop_back();
        return index;
    }
    particles_.push_back(Particle{});
    return particles_.size() - 1;
}

std::uint32_t ParticleWorld::emitBurstAtWorld(Scene& scene, NodeId emitter_node,
                                              const glm::vec3& world_position, std::uint32_t count) {
    if (count == 0) {
        return 0;
    }
    syncEmitters(scene);
    Node* node = scene.find(emitter_node);
    if (!node || !node->particle_emitter.has_value()) {
        return 0;
    }

    EmitterRuntime* runtime = nullptr;
    for (EmitterRuntime& candidate : emitters_) {
        if (candidate.node_id == emitter_node) {
            runtime = &candidate;
            break;
        }
    }
    if (!runtime) {
        return 0;
    }

    const std::uint64_t emit_before = runtime->next_emit_index;
    spawnFromEmitter(scene, *runtime, *node->particle_emitter, *node, count, &world_position);
    const std::uint64_t emit_after = runtime->next_emit_index;

    std::uint32_t alive = 0;
    for (const Particle& p : particles_) {
        if (p.alive) {
            ++alive;
        }
    }
    stats_.alive = alive;
    return static_cast<std::uint32_t>(emit_after - emit_before);
}

void ParticleWorld::spawnFromEmitter(Scene& scene, EmitterRuntime& runtime, const ParticleEmitter& desc,
                                     const Node& node, std::uint32_t count,
                                     const glm::vec3* origin_override) {
    (void)scene;
    if (count == 0 || !desc.enabled || runtime.finished) {
        return;
    }

    std::uint32_t alive = 0;
    for (const Particle& p : particles_) {
        if (p.alive && p.emitter_node == runtime.node_id) {
            ++alive;
        }
    }

    const std::uint64_t seed = desc.seed_override != 0 ? desc.seed_override : scene_seed_;
    const glm::mat4 world = node.world;
    const glm::vec3 origin = origin_override ? *origin_override : glm::vec3(world[3]);

    for (std::uint32_t i = 0; i < count; ++i) {
        if (alive >= desc.max_particles) {
            break; // deterministic refuse-when-full
        }
        const std::uint64_t emit_index = runtime.next_emit_index++;
        const std::size_t slot = allocateParticle();
        Particle& p = particles_[slot];
        p = Particle{};
        p.alive = true;
        p.emitter_node = runtime.node_id;
        p.emit_index = emit_index;
        p.age = 0.0f;
        p.lifetime = std::max(0.001f, particleRandomRange(seed, runtime.node_id, emit_index, kChanLifetime,
                                                          desc.lifetime_min, desc.lifetime_max));
        p.size0 = particleRandomRange(seed, runtime.node_id, emit_index, kChanSize, desc.size_min,
                                      desc.size_max);
        p.size_end_mul = desc.size_end_multiplier;
        p.color0 = desc.color_start;
        p.color1 = desc.color_end;
        p.mesh = desc.render_mesh;
        p.unlit = desc.unlit;
        p.world_space = desc.world_space;

        glm::vec3 local_offset(0.0f);
        glm::vec3 direction(0.0f, 1.0f, 0.0f);
        switch (desc.spawn_shape) {
        case ParticleSpawnShape::Point:
            direction = glm::normalize(glm::mat3(world) * glm::vec3(0.0f, 1.0f, 0.0f));
            break;
        case ParticleSpawnShape::Sphere: {
            const glm::vec3 unit = randomUnitVector(seed, runtime.node_id, emit_index);
            local_offset = unit * (desc.spawn_radius *
                                   particleRandom01(seed, runtime.node_id, emit_index, kChanPosX));
            direction = unit;
            break;
        }
        case ParticleSpawnShape::Box: {
            local_offset = glm::vec3(
                (particleRandom01(seed, runtime.node_id, emit_index, kChanPosX) * 2.0f - 1.0f) *
                    desc.spawn_radius,
                (particleRandom01(seed, runtime.node_id, emit_index, kChanPosY) * 2.0f - 1.0f) *
                    desc.spawn_box_height,
                (particleRandom01(seed, runtime.node_id, emit_index, kChanPosZ) * 2.0f - 1.0f) *
                    desc.spawn_radius);
            direction = coneDirection(world, desc.cone_angle_degrees, seed, runtime.node_id, emit_index);
            break;
        }
        case ParticleSpawnShape::Ring: {
            // Cylinder ring in local XZ: angle around Y, radius ≈ spawn_radius,
            // optional vertical jitter via spawn_box_height.
            constexpr float kTwoPi = 6.28318530718f;
            const float angle =
                particleRandom01(seed, runtime.node_id, emit_index, kChanPosX) * kTwoPi;
            const float radial_jitter =
                0.9f + 0.1f * particleRandom01(seed, runtime.node_id, emit_index, kChanPosZ);
            const float radius = desc.spawn_radius * radial_jitter;
            const float y =
                (particleRandom01(seed, runtime.node_id, emit_index, kChanPosY) * 2.0f - 1.0f) *
                desc.spawn_box_height;
            local_offset = glm::vec3(std::cos(angle) * radius, y, std::sin(angle) * radius);
            direction = coneDirection(world, desc.cone_angle_degrees, seed, runtime.node_id, emit_index);
            break;
        }
        case ParticleSpawnShape::Cone:
        default:
            direction = coneDirection(world, desc.cone_angle_degrees, seed, runtime.node_id, emit_index);
            local_offset = glm::vec3(0.0f);
            break;
        }

        if (desc.world_space) {
            p.position = origin + glm::mat3(world) * local_offset;
            p.velocity = direction * particleRandomRange(seed, runtime.node_id, emit_index, kChanSpeed,
                                                         desc.speed_min, desc.speed_max);
        } else {
            p.position = local_offset;
            p.velocity = (glm::inverse(glm::mat3(world)) * direction) *
                         particleRandomRange(seed, runtime.node_id, emit_index, kChanSpeed, desc.speed_min,
                                             desc.speed_max);
        }
        ++alive;
    }
}

void ParticleWorld::stepOnce(Scene& scene, float dt) {
    syncEmitters(scene);
    stats_.emitters = static_cast<std::uint32_t>(emitters_.size());

    for (EmitterRuntime& runtime : emitters_) {
        Node* node = scene.find(runtime.node_id);
        if (!node || !node->particle_emitter.has_value()) {
            continue;
        }
        const ParticleEmitter& desc = *node->particle_emitter;
        if (!desc.enabled || runtime.finished) {
            continue;
        }

        runtime.age_seconds += static_cast<double>(dt);
        if (runtime.age_seconds < static_cast<double>(desc.start_delay_seconds)) {
            continue;
        }

        if (!runtime.burst_fired && desc.burst_count > 0) {
            spawnFromEmitter(scene, runtime, desc, *node, desc.burst_count);
            runtime.burst_fired = true;
        }

        if (desc.duration_seconds > 0.0f &&
            runtime.age_seconds >= static_cast<double>(desc.start_delay_seconds + desc.duration_seconds)) {
            if (!desc.looping) {
                runtime.finished = true;
            } else {
                runtime.age_seconds = static_cast<double>(desc.start_delay_seconds);
                runtime.burst_fired = false;
                runtime.emit_accumulator = 0.0;
            }
        }

        if (!runtime.finished && desc.rate_per_second > 0.0f) {
            runtime.emit_accumulator += static_cast<double>(desc.rate_per_second) * static_cast<double>(dt);
            const std::uint32_t to_spawn = static_cast<std::uint32_t>(runtime.emit_accumulator);
            if (to_spawn > 0) {
                runtime.emit_accumulator -= static_cast<double>(to_spawn);
                spawnFromEmitter(scene, runtime, desc, *node, to_spawn);
            }
        }
    }

    std::uint32_t alive = 0;
    for (std::size_t i = 0; i < particles_.size(); ++i) {
        Particle& p = particles_[i];
        if (!p.alive) {
            continue;
        }

        Node* node = scene.find(p.emitter_node);
        ParticleEmitter desc = makeDefaultParticleEmitter();
        glm::mat4 world(1.0f);
        if (node && node->particle_emitter.has_value()) {
            desc = *node->particle_emitter;
            world = node->world;
        }

        p.age += dt;
        if (p.age >= p.lifetime) {
            p.alive = false;
            free_list_.push_back(i);
            continue;
        }

        p.velocity += desc.gravity * dt;
        p.velocity *= std::max(0.0f, 1.0f - desc.drag * dt);

        if (desc.fluid_coupled && fluid_world_) {
            NodeId fluid_node = 0;
            for (NodeId cur = p.emitter_node; cur != 0;) {
                const Node* fluid_candidate = scene.find(cur);
                if (!fluid_candidate) {
                    break;
                }
                if (fluid_candidate->particle_fluid.has_value() &&
                    fluid_candidate->particle_fluid->drive_particles) {
                    fluid_node = cur;
                    break;
                }
                if (!fluid_candidate->parent.has_value()) {
                    break;
                }
                cur = *fluid_candidate->parent;
            }
            if (fluid_node != 0 && node) {
                const glm::vec3 world_pos =
                    p.world_space ? p.position
                                  : glm::vec3(node->world * glm::vec4(p.position, 1.0f));
                const glm::vec3 fluid_vel =
                    fluid_world_->sampleFluidVelocity(scene, fluid_node, world_pos);
                // Softly blend toward the fluid field so particles rise/fall with the lamp.
                const float blend = std::min(1.0f, 3.5f * dt);
                p.velocity = p.velocity * (1.0f - blend) + fluid_vel * blend;
            }
        }

        if (desc.world_space || !node) {
            p.position += p.velocity * dt;
        } else {
            p.position += p.velocity * dt;
            // Keep local; rendered with emitter world matrix.
            (void)world;
        }
        ++alive;
    }

    stats_.alive = alive;
    ++global_step_;
    sim_time_seconds_ += static_cast<double>(dt);
}

void ParticleWorld::simulate(Scene& scene, std::uint64_t steps, float dt) {
    stats_.steps = static_cast<std::uint32_t>(steps);
    for (std::uint64_t i = 0; i < steps; ++i) {
        stepOnce(scene, dt);
    }
    if (steps == 0) {
        syncEmitters(scene);
        stats_.emitters = static_cast<std::uint32_t>(emitters_.size());
        std::uint32_t alive = 0;
        for (const Particle& p : particles_) {
            if (p.alive) {
                ++alive;
            }
        }
        stats_.alive = alive;
    }
}

void ParticleWorld::rebuildToTime(Scene& scene, double production_time_seconds, float fixed_dt) {
    reset();
    syncEmitters(scene);
    if (fixed_dt <= 1e-8f || production_time_seconds <= 0.0) {
        return;
    }
    const std::uint64_t steps =
        static_cast<std::uint64_t>(std::floor(production_time_seconds / static_cast<double>(fixed_dt)));
    // Cap rebuild work for interactive seeks; production seeks can raise this later.
    const std::uint64_t capped = std::min(steps, static_cast<std::uint64_t>(60.0 / fixed_dt * 30.0));
    simulate(scene, capped, fixed_dt);
}

void ParticleWorld::collectInstances(const Scene& scene, std::vector<ParticleInstanceBatch>& out) const {
    out.clear();
    ParticleInstanceBatch cube_lit{};
    cube_lit.mesh = ParticleRenderMesh::Cube;
    cube_lit.unlit = false;
    ParticleInstanceBatch cube_unlit{};
    cube_unlit.mesh = ParticleRenderMesh::Cube;
    cube_unlit.unlit = true;
    ParticleInstanceBatch sphere_lit{};
    sphere_lit.mesh = ParticleRenderMesh::Sphere;
    sphere_lit.unlit = false;
    ParticleInstanceBatch sphere_unlit{};
    sphere_unlit.mesh = ParticleRenderMesh::Sphere;
    sphere_unlit.unlit = true;

    for (const Particle& p : particles_) {
        if (!p.alive) {
            continue;
        }
        const float t = std::clamp(p.age / std::max(p.lifetime, 1e-5f), 0.0f, 1.0f);
        const float size = p.size0 * glm::mix(1.0f, p.size_end_mul, t);
        const glm::vec4 color = glm::mix(p.color0, p.color1, t);

        glm::mat4 model(1.0f);
        if (p.world_space) {
            model = glm::translate(glm::mat4(1.0f), p.position);
        } else if (const Node* node = scene.find(p.emitter_node)) {
            model = node->world * glm::translate(glm::mat4(1.0f), p.position);
        } else {
            model = glm::translate(glm::mat4(1.0f), p.position);
        }
        model = glm::scale(model, glm::vec3(size));

        GpuMeshInstance instance{};
        fillGpuMeshInstance(instance, model, glm::vec3(-0.5f), glm::vec3(0.5f));
        instance.color = color;

        if (p.mesh == ParticleRenderMesh::Sphere) {
            (p.unlit ? sphere_unlit : sphere_lit).instances.push_back(instance);
        } else {
            (p.unlit ? cube_unlit : cube_lit).instances.push_back(instance);
        }
    }

    auto push_if = [&](ParticleInstanceBatch& batch) {
        if (!batch.instances.empty()) {
            out.push_back(std::move(batch));
        }
    };
    push_if(cube_unlit);
    push_if(cube_lit);
    push_if(sphere_unlit);
    push_if(sphere_lit);
}

std::uint64_t ParticleWorld::stateHash() const {
    std::uint64_t hash = 14695981039346656037ull;
    auto mix = [&](std::uint64_t v) {
        hash ^= v;
        hash *= 1099511628211ull;
    };
    mix(scene_seed_);
    mix(global_step_);
    for (const Particle& p : particles_) {
        if (!p.alive) {
            continue;
        }
        mix(p.emitter_node);
        mix(p.emit_index);
        mix(static_cast<std::uint64_t>(particleHashU32(
            static_cast<std::uint64_t>(p.position.x * 1000.0f) ^
            (static_cast<std::uint64_t>(p.position.y * 1000.0f) << 20) ^
            (static_cast<std::uint64_t>(p.position.z * 1000.0f) << 40))));
    }
    return hash;
}

} // namespace genesis
