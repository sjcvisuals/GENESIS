#include "particles/ParticleWorld.hpp"

#include "particles/FluidWorld.hpp"
#include "particles/ParticleRng.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace genesis {

namespace {

constexpr std::uint32_t kChanLifetime = 1;
constexpr std::uint32_t kChanSpeed = 2;
constexpr std::uint32_t kChanSize = 3;
constexpr std::uint32_t kChanDirX = 4;
constexpr std::uint32_t kChanDirY = 5;
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
    return glm::normalize(glm::mat3(world) * local);
}

// Cheap deterministic value noise (not UE noise — public hash trick).
float hashNoise3(const glm::vec3& p) {
    const float n = std::sin(glm::dot(p, glm::vec3(12.9898f, 78.233f, 37.719f))) * 43758.5453f;
    return n - std::floor(n);
}

glm::vec3 curlNoiseApprox(const glm::vec3& p, float freq) {
    const glm::vec3 q = p * freq;
    const float n0 = hashNoise3(q);
    const float n1 = hashNoise3(q + glm::vec3(17.1f, 9.2f, 3.4f));
    const float n2 = hashNoise3(q + glm::vec3(5.7f, 21.3f, 11.8f));
    return glm::normalize(glm::vec3(n0 * 2.0f - 1.0f, n1 * 2.0f - 1.0f, n2 * 2.0f - 1.0f) +
                          glm::vec3(1e-5f));
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
        emitter.timed_bursts_fired = 0;
        emitter.finished = false;
        emitter.alive_count = 0;
        emitter.has_last_pos = false;
        emitter.velocity = glm::vec3(0.0f);
        emitter.distance_spawn_accumulator = 0.0;
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

ParticleWorld::EmitterRuntime* ParticleWorld::findEmitterRuntime(NodeId id) {
    for (EmitterRuntime& runtime : emitters_) {
        if (runtime.node_id == id) {
            return &runtime;
        }
    }
    return nullptr;
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

void ParticleWorld::recountAlive() {
    std::uint32_t alive = 0;
    for (EmitterRuntime& runtime : emitters_) {
        runtime.alive_count = 0;
    }
    for (const Particle& p : particles_) {
        if (!p.alive) {
            continue;
        }
        ++alive;
        if (EmitterRuntime* runtime = findEmitterRuntime(p.emitter_node)) {
            ++runtime->alive_count;
        }
    }
    stats_.alive = alive;
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
    EmitterRuntime* runtime = findEmitterRuntime(emitter_node);
    if (!runtime) {
        return 0;
    }
    const std::uint64_t emit_before = runtime->next_emit_index;
    spawnFromEmitter(scene, *runtime, *node->particle_emitter, *node, count, &world_position);
    recountAlive();
    return static_cast<std::uint32_t>(runtime->next_emit_index - emit_before);
}

void ParticleWorld::spawnFromEmitter(Scene& scene, EmitterRuntime& runtime, const ParticleEmitter& desc,
                                     const Node& node, std::uint32_t count,
                                     const glm::vec3* origin_override) {
    (void)scene;
    if (count == 0 || !desc.enabled || runtime.finished) {
        return;
    }

    const std::uint64_t seed = desc.seed_override != 0 ? desc.seed_override : scene_seed_;
    const glm::mat4 world = node.world;
    const glm::vec3 origin = origin_override ? *origin_override : glm::vec3(world[3]);

    ParticleEmitter authored = desc;
    syncEmitterCurvesFromEndpoints(authored);

    for (std::uint32_t i = 0; i < count; ++i) {
        if (runtime.alive_count >= desc.max_particles) {
            break;
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
        p.sprite_aspect = desc.sprite_aspect;
        p.velocity_stretch = desc.velocity_stretch;
        p.mesh = desc.render_mesh;
        p.unlit = desc.unlit;
        p.additive = desc.additive;
        p.world_space = desc.world_space;
        p.size_curve = authored.size_over_life;
        p.color_curve = authored.color_over_life;
        p.trail_enabled = desc.trail_enabled;
        p.trail_max_segments =
            static_cast<std::uint8_t>(std::clamp(static_cast<int>(desc.trail_segments), 2, 12));
        p.trail_sample_distance = std::max(0.005f, desc.trail_sample_distance);
        p.trail_width = std::max(0.0f, desc.trail_width);
        p.trail_width_end = std::max(0.0f, desc.trail_width_end);
        p.trail_alpha = std::clamp(desc.trail_alpha, 0.0f, 1.0f);
        p.trail_lifetime = std::max(0.05f, desc.trail_lifetime);
        p.trail_count = 0;
        p.trail_dist_accum = 0.0f;
        p.trail_has_sample = false;

        glm::vec3 local_offset(0.0f);
        glm::vec3 direction = glm::normalize(glm::mat3(world) * glm::vec3(0.0f, 1.0f, 0.0f));
        switch (desc.spawn_shape) {
        case ParticleSpawnShape::Point:
            break;
        case ParticleSpawnShape::Sphere: {
            // Offset only — do NOT steal velocity direction (that sprayed sideways and
            // turned soft discs + trails into horizontal streaks).
            const glm::vec3 unit = randomUnitVector(seed, runtime.node_id, emit_index);
            local_offset = unit * (desc.spawn_radius *
                                   particleRandom01(seed, runtime.node_id, emit_index, kChanPosX));
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
            break;
        }

        switch (desc.velocity_mode) {
        case ParticleVelocityMode::FromPoint: {
            const glm::vec3 world_pos = origin + glm::mat3(world) * local_offset;
            const glm::vec3 radial = world_pos - origin;
            if (glm::dot(radial, radial) > 1e-8f) {
                direction = glm::normalize(radial);
            }
            break;
        }
        case ParticleVelocityMode::Random:
            direction = randomUnitVector(seed, runtime.node_id, emit_index);
            break;
        case ParticleVelocityMode::Cone:
        default:
            // Always apply cone (spawn shapes must not leave a non-up direction).
            direction =
                coneDirection(world, desc.cone_angle_degrees, seed, runtime.node_id, emit_index);
            break;
        }

        const float speed = particleRandomRange(seed, runtime.node_id, emit_index, kChanSpeed,
                                                desc.speed_min, desc.speed_max);
        glm::vec3 birth_vel = direction * speed;
        if (desc.inherit_velocity > 0.0f) {
            birth_vel += runtime.velocity * std::clamp(desc.inherit_velocity, 0.0f, 1.0f);
        }

        if (desc.world_space) {
            p.position = origin + glm::mat3(world) * local_offset;
            p.velocity = birth_vel;
        } else {
            p.position = local_offset;
            p.velocity = (glm::inverse(glm::mat3(world)) * birth_vel);
        }
        ++runtime.alive_count;
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
        const glm::vec3 world_pos = glm::vec3(node->world[3]);
        float traveled = 0.0f;
        if (runtime.has_last_pos && dt > 1e-8f) {
            const glm::vec3 delta = world_pos - runtime.last_world_pos;
            traveled = glm::length(delta);
            runtime.velocity = delta / dt;
        }
        runtime.last_world_pos = world_pos;
        runtime.has_last_pos = true;

        if (!desc.enabled || runtime.finished) {
            continue;
        }

        runtime.age_seconds += static_cast<double>(dt);
        if (runtime.age_seconds < static_cast<double>(desc.start_delay_seconds)) {
            continue;
        }

        const double local_age = runtime.age_seconds - static_cast<double>(desc.start_delay_seconds);

        if (!runtime.burst_fired && desc.burst_count > 0) {
            spawnFromEmitter(scene, runtime, desc, *node, desc.burst_count);
            runtime.burst_fired = true;
        }

        for (std::uint8_t bi = 0; bi < desc.burst_entries && bi < desc.bursts.size(); ++bi) {
            const std::uint8_t bit = static_cast<std::uint8_t>(1u << bi);
            if ((runtime.timed_bursts_fired & bit) != 0) {
                continue;
            }
            if (local_age >= static_cast<double>(desc.bursts[bi].time_seconds) &&
                desc.bursts[bi].count > 0) {
                spawnFromEmitter(scene, runtime, desc, *node, desc.bursts[bi].count);
                runtime.timed_bursts_fired =
                    static_cast<std::uint8_t>(runtime.timed_bursts_fired | bit);
            }
        }

        if (desc.duration_seconds > 0.0f &&
            runtime.age_seconds >= static_cast<double>(desc.start_delay_seconds + desc.duration_seconds)) {
            if (!desc.looping) {
                runtime.finished = true;
            } else {
                runtime.age_seconds = static_cast<double>(desc.start_delay_seconds);
                runtime.burst_fired = false;
                runtime.timed_bursts_fired = 0;
                runtime.emit_accumulator = 0.0;
                runtime.distance_spawn_accumulator = 0.0;
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

        // Niagara-style spawn-by-distance (Emitter Update).
        if (!runtime.finished && desc.spawn_per_unit > 0.0f && traveled > 0.0f) {
            runtime.distance_spawn_accumulator +=
                static_cast<double>(traveled) * static_cast<double>(desc.spawn_per_unit);
            const std::uint32_t to_spawn =
                static_cast<std::uint32_t>(runtime.distance_spawn_accumulator);
            if (to_spawn > 0) {
                runtime.distance_spawn_accumulator -= static_cast<double>(to_spawn);
                spawnFromEmitter(scene, runtime, desc, *node, to_spawn);
            }
        }
    }

    std::uint32_t alive = 0;
    for (EmitterRuntime& runtime : emitters_) {
        runtime.alive_count = 0;
    }

    for (std::size_t i = 0; i < particles_.size(); ++i) {
        Particle& p = particles_[i];
        if (!p.alive) {
            continue;
        }

        Node* node = scene.find(p.emitter_node);
        ParticleEmitter desc = makeDefaultParticleEmitter();
        if (node && node->particle_emitter.has_value()) {
            desc = *node->particle_emitter;
        }

        p.age += dt;
        if (p.age >= p.lifetime) {
            p.alive = false;
            free_list_.push_back(i);
            continue;
        }

        glm::vec3 world_pos = p.position;
        if (!p.world_space && node) {
            world_pos = glm::vec3(node->world * glm::vec4(p.position, 1.0f));
        }

        if (desc.force_gravity) {
            p.velocity += desc.gravity * dt;
        }
        if (desc.force_accel) {
            p.velocity += desc.acceleration * dt;
        }
        if (desc.force_drag) {
            p.velocity *= std::max(0.0f, 1.0f - desc.drag * dt);
        }
        if (desc.force_point && std::fabs(desc.point_force_strength) > 1e-6f) {
            glm::vec3 to_point = desc.point_force_position - world_pos;
            const float dist = std::max(glm::length(to_point), desc.point_force_softness);
            to_point /= dist;
            p.velocity += to_point * (desc.point_force_strength / (dist * dist + 1e-4f)) * dt;
        }
        if (desc.force_vortex && std::fabs(desc.vortex_strength) > 1e-6f) {
            const glm::vec3 axis = glm::length(desc.vortex_axis) > 1e-5f
                                       ? glm::normalize(desc.vortex_axis)
                                       : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 radial = world_pos - glm::dot(world_pos, axis) * axis;
            const float radial_len = glm::length(radial);
            if (radial_len > 1e-4f && radial_len < desc.vortex_radius) {
                const glm::vec3 tangent = glm::normalize(glm::cross(axis, radial));
                const float falloff = 1.0f - (radial_len / std::max(desc.vortex_radius, 1e-4f));
                p.velocity += tangent * (desc.vortex_strength * falloff) * dt;
            }
        }
        if (desc.force_noise && desc.noise_strength > 1e-6f) {
            const glm::vec3 n =
                curlNoiseApprox(world_pos + glm::vec3(static_cast<float>(sim_time_seconds_)),
                                desc.noise_frequency);
            p.velocity += n * desc.noise_strength * dt;
        }

        if (desc.fluid_coupled && fluid_world_ && node) {
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
            if (fluid_node != 0) {
                const glm::vec3 fluid_vel =
                    fluid_world_->sampleFluidVelocity(scene, fluid_node, world_pos);
                const float blend = std::min(1.0f, 3.5f * dt);
                p.velocity = p.velocity * (1.0f - blend) + fluid_vel * blend;
            }
        }

        p.position += p.velocity * dt;

        if (!p.world_space && node) {
            world_pos = glm::vec3(node->world * glm::vec4(p.position, 1.0f));
        } else {
            world_pos = p.position;
        }

        // Trail Renderer: sample motion history (deterministic; same on every GPU).
        if (p.trail_enabled) {
            if (!p.trail_has_sample) {
                p.trail_last_sample = world_pos;
                p.trail_has_sample = true;
                p.trail_pos[0] = world_pos;
                p.trail_count = 1;
            } else {
                const float moved = glm::length(world_pos - p.trail_last_sample);
                p.trail_dist_accum += moved;
                p.trail_last_sample = world_pos;
                const float sample_dist = std::max(0.005f, p.trail_sample_distance);
                while (p.trail_dist_accum >= sample_dist) {
                    p.trail_dist_accum -= sample_dist;
                    const std::uint8_t max_seg = std::min(p.trail_max_segments, kMaxTrailPoints);
                    if (p.trail_count < max_seg) {
                        p.trail_pos[p.trail_count++] = world_pos;
                    } else {
                        for (std::uint8_t ti = 1; ti < max_seg; ++ti) {
                            p.trail_pos[ti - 1] = p.trail_pos[ti];
                        }
                        p.trail_pos[max_seg - 1] = world_pos;
                        p.trail_count = max_seg;
                    }
                }
                // Keep the newest sample pinned to the live particle position.
                if (p.trail_count > 0) {
                    p.trail_pos[p.trail_count - 1] = world_pos;
                }
            }
        }

        if (desc.kill_plane && world_pos.y < desc.kill_plane_y) {
            p.alive = false;
            free_list_.push_back(i);
            continue;
        }
        if (desc.kill_box) {
            if (world_pos.x < desc.kill_box_min.x || world_pos.x > desc.kill_box_max.x ||
                world_pos.y < desc.kill_box_min.y || world_pos.y > desc.kill_box_max.y ||
                world_pos.z < desc.kill_box_min.z || world_pos.z > desc.kill_box_max.z) {
                p.alive = false;
                free_list_.push_back(i);
                continue;
            }
        }

        ++alive;
        if (EmitterRuntime* er = findEmitterRuntime(p.emitter_node)) {
            ++er->alive_count;
        }
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
        recountAlive();
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
    const std::uint64_t capped = std::min(steps, static_cast<std::uint64_t>(60.0 / fixed_dt * 30.0));
    simulate(scene, capped, fixed_dt);
}

void ParticleWorld::collectInstances(const Scene& scene, std::vector<ParticleInstanceBatch>& out,
                                     const ParticleViewBasis& view) const {
    // Batches: mesh × unlit × additive (12 slots). Keeps GPU instancing cheap.
    out.clear();
    out.resize(12);
    auto set_batch = [&](std::size_t i, ParticleRenderMesh mesh, bool unlit, bool additive) {
        out[i] = {mesh, unlit, additive, {}};
    };
    set_batch(0, ParticleRenderMesh::Cube, false, false);
    set_batch(1, ParticleRenderMesh::Cube, true, false);
    set_batch(2, ParticleRenderMesh::Cube, false, true);
    set_batch(3, ParticleRenderMesh::Cube, true, true);
    set_batch(4, ParticleRenderMesh::Sphere, false, false);
    set_batch(5, ParticleRenderMesh::Sphere, true, false);
    set_batch(6, ParticleRenderMesh::Sphere, false, true);
    set_batch(7, ParticleRenderMesh::Sphere, true, true);
    set_batch(8, ParticleRenderMesh::Sprite, false, false);
    set_batch(9, ParticleRenderMesh::Sprite, true, false);
    set_batch(10, ParticleRenderMesh::Sprite, false, true);
    set_batch(11, ParticleRenderMesh::Sprite, true, true);

    // Billboard basis from the view camera. Gram–Schmidt so soft discs stay round
    // (sheared right/up collapses quads into horizontal/vertical streaks).
    glm::vec3 cam_right =
        view.valid ? glm::normalize(view.right) : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 cam_up_in = view.valid ? glm::normalize(view.up) : glm::vec3(0.0f, 1.0f, 0.0f);
    // Remove look-axis shear from up, then rebuild a true orthonormal frame.
    const glm::vec3 cam_forward =
        glm::normalize(glm::cross(cam_right, cam_up_in) + glm::vec3(1e-5f));
    const glm::vec3 cam_up =
        glm::normalize(glm::cross(cam_forward, cam_right) + glm::vec3(1e-5f));
    cam_right = glm::normalize(glm::cross(cam_up, cam_forward) + glm::vec3(1e-5f));

    for (const Particle& p : particles_) {
        if (!p.alive) {
            continue;
        }
        const float t = std::clamp(p.age / std::max(p.lifetime, 1e-5f), 0.0f, 1.0f);
        float size_mul = p.size_curve.count > 0 ? p.size_curve.evaluate(t)
                                                 : glm::mix(1.0f, 0.25f, t);

        // Scale by Speed — look up emitter flags (cheap; module data on node).
        if (const Node* emitter_node = scene.find(p.emitter_node);
            emitter_node && emitter_node->particle_emitter.has_value() &&
            emitter_node->particle_emitter->scale_by_speed) {
            const ParticleEmitter& pe = *emitter_node->particle_emitter;
            const float speed = glm::length(p.velocity);
            const float span = std::max(1e-4f, pe.scale_by_speed_range_max - pe.scale_by_speed_range_min);
            const float u = std::clamp((speed - pe.scale_by_speed_range_min) / span, 0.0f, 1.0f);
            size_mul *= glm::mix(pe.scale_by_speed_mul_min, pe.scale_by_speed_mul_max, u);
        }

        // Soft floor — tiny sprites strobe as fireflies; keep a readable circle.
        const float size = std::max(0.04f, p.size0 * size_mul);
        glm::vec4 color =
            p.color_curve.count > 0 ? p.color_curve.evaluate(t) : glm::vec4(1.0f, 1.0f, 1.0f, 1.0f - t);
        // Gentle energy budget (alpha blend or soft additive — never hot cores).
        color.r = std::min(color.r, 0.9f);
        color.g = std::min(color.g, 0.92f);
        color.b = std::min(color.b, 0.96f);
        color.a = std::clamp(color.a, 0.0f, 0.72f);

        glm::vec3 world_pos = p.position;
        if (!p.world_space) {
            if (const Node* node = scene.find(p.emitter_node)) {
                world_pos = glm::vec3(node->world * glm::vec4(p.position, 1.0f));
            }
        }

        std::size_t mesh_base = 8;
        switch (p.mesh) {
        case ParticleRenderMesh::Cube:
            mesh_base = 0;
            break;
        case ParticleRenderMesh::Sphere:
            mesh_base = 4;
            break;
        case ParticleRenderMesh::Sprite:
        default:
            mesh_base = 8;
            break;
        }
        const std::size_t batch_index = mesh_base + (p.additive ? 2u : 0u) + (p.unlit ? 1u : 0u);

        auto push_sprite = [&](const glm::mat4& model, const glm::vec4& tint) {
            GpuMeshInstance instance{};
            fillGpuMeshInstance(instance, model, glm::vec3(-0.5f), glm::vec3(0.5f));
            instance.color = tint;
            out[batch_index].instances.push_back(instance);
        };

        // Trail Renderer: soft disc chain along history, width head→tip (point).
        // trail_width / trail_width_end are fractions of the particle head size.
        if (p.trail_enabled && p.trail_count >= 2 && p.mesh == ParticleRenderMesh::Sprite) {
            const float trail_age_fade =
                std::clamp(1.0f - (p.age / std::max(p.trail_lifetime * 2.5f, 0.1f)) * 0.25f, 0.5f,
                           1.0f);
            const float denom = static_cast<float>(std::max<int>(p.trail_count - 1, 1));
            const float head_w = std::max(size, 0.04f);
            // Skip newest sample — drawn as the round head below.
            for (std::uint8_t si = 0; si + 1 < p.trail_count; ++si) {
                const float u = static_cast<float>(si) / denom; // 0 = tip, 1 ≈ head
                const float width_frac =
                    glm::mix(std::max(p.trail_width_end, 0.0f), std::max(p.trail_width, 0.05f), u);
                const float width = head_w * width_frac;
                // Keep tip readable in stills but never a sub-pixel spark.
                if (width < std::max(0.012f, head_w * 0.1f)) {
                    continue;
                }
                const float tip_fade = 0.2f + 0.8f * u;
                glm::vec4 trail_color = color;
                trail_color.a *= p.trail_alpha * tip_fade * trail_age_fade;
                trail_color.a = std::min(trail_color.a, 0.5f);

                glm::mat4 model(1.0f);
                model[0] = glm::vec4(cam_right * width, 0.0f);
                model[1] = glm::vec4(cam_up * width, 0.0f);
                model[2] = glm::vec4(cam_forward, 0.0f);
                model[3] = glm::vec4(p.trail_pos[si], 1.0f);
                push_sprite(model, trail_color);
            }
        }

        // Primary soft circle head — round disc (aspect locked to 1 unless stretch requested).
        glm::mat4 model(1.0f);
        if (p.mesh == ParticleRenderMesh::Sprite) {
            const float speed = glm::length(p.velocity);
            // Round head by default; velocity_stretch is opt-in (off for gentle look).
            float hx = size;
            float hy = size;
            if (p.velocity_stretch > 1e-4f && speed > 1e-4f) {
                const float aspect = std::max(p.sprite_aspect, 0.05f);
                hx = size * aspect;
                hy = size * (1.0f + speed * p.velocity_stretch);
            }
            model[0] = glm::vec4(cam_right * hx, 0.0f);
            model[1] = glm::vec4(cam_up * hy, 0.0f);
            model[2] = glm::vec4(cam_forward, 0.0f);
            model[3] = glm::vec4(world_pos, 1.0f);
        } else {
            model = glm::translate(glm::mat4(1.0f), world_pos);
            model = glm::scale(model, glm::vec3(size));
        }
        push_sprite(model, color);
    }

    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const ParticleInstanceBatch& b) { return b.instances.empty(); }),
              out.end());
}

std::uint64_t ParticleWorld::stateHash() const {
    std::uint64_t h = 14695981039346656037ull;
    auto mix = [&](std::uint64_t v) {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    };
    mix(scene_seed_);
    mix(global_step_);
    mix(stats_.alive);
    for (const Particle& p : particles_) {
        if (!p.alive) {
            continue;
        }
        mix(p.emitter_node);
        mix(p.emit_index);
        mix(static_cast<std::uint64_t>(p.age * 1000.0f));
        mix(static_cast<std::uint64_t>(p.position.x * 1000.0f) ^
            (static_cast<std::uint64_t>(p.position.y * 1000.0f) << 20) ^
            (static_cast<std::uint64_t>(p.position.z * 1000.0f) << 40));
    }
    return h;
}

} // namespace genesis
