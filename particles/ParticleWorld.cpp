#include "particles/ParticleWorld.hpp"

#include "renderer/vulkan/VulkanContext.hpp"

#include "particles/FluidWorld.hpp"
#include "particles/ParticleRng.hpp"
#include "scene/SceneVisibility.hpp"

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
constexpr std::uint32_t kChanColor = 10;

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

// Sample a local offset inside the authored spawn shape (CPU birth; GPU sim moves after).
glm::vec3 sampleSpawnLocalOffset(const ParticleEmitter& desc, std::uint64_t seed,
                                 std::uint64_t emitter_id, std::uint64_t emit_index) {
    const float ux = particleRandom01(seed, emitter_id, emit_index, kChanPosX);
    const float uy = particleRandom01(seed, emitter_id, emit_index, kChanPosY);
    const float uz = particleRandom01(seed, emitter_id, emit_index, kChanPosZ);
    const ParticleSpawnDistribution dist = desc.spawn_distribution;
    const float radius = std::max(0.0f, desc.spawn_radius);
    const float half_h = std::max(0.0f, desc.spawn_box_height);

    switch (desc.spawn_shape) {
    case ParticleSpawnShape::Point:
        return glm::vec3(0.0f);
    case ParticleSpawnShape::Sphere: {
        const glm::vec3 unit = randomUnitVector(seed, emitter_id, emit_index);
        if (dist == ParticleSpawnDistribution::Surface) {
            return unit * radius;
        }
        if (dist == ParticleSpawnDistribution::VolumeUniform) {
            // Density-uniform ball: r = R * u^(1/3).
            return unit * (radius * std::cbrt(ux));
        }
        // Volume (legacy): center-biased r = R * u.
        return unit * (radius * ux);
    }
    case ParticleSpawnShape::Box: {
        const float hx = radius;
        const float hy = half_h;
        const float hz = radius;
        if (dist == ParticleSpawnDistribution::Surface) {
            // Area-weighted face pick on the AABB shell.
            const float ax = 2.0f * hy * hz; // ±X faces
            const float ay = 2.0f * hx * hz; // ±Y faces
            const float az = 2.0f * hx * hy; // ±Z faces
            const float total = std::max(1e-8f, ax + ay + az);
            float pick = ux * total;
            if (pick < ax) {
                const float side = (pick < ax * 0.5f) ? -1.0f : 1.0f;
                return glm::vec3(side * hx, (uy * 2.0f - 1.0f) * hy, (uz * 2.0f - 1.0f) * hz);
            }
            pick -= ax;
            if (pick < ay) {
                const float side = (pick < ay * 0.5f) ? -1.0f : 1.0f;
                return glm::vec3((uy * 2.0f - 1.0f) * hx, side * hy, (uz * 2.0f - 1.0f) * hz);
            }
            {
                const float side = (uy < 0.5f) ? -1.0f : 1.0f;
                return glm::vec3((uz * 2.0f - 1.0f) * hx, (uy * 2.0f - 1.0f) * hy, side * hz);
            }
        }
        // Volume / VolumeUniform: AABB is already density-uniform.
        return glm::vec3((ux * 2.0f - 1.0f) * hx, (uy * 2.0f - 1.0f) * hy, (uz * 2.0f - 1.0f) * hz);
    }
    case ParticleSpawnShape::Ring: {
        constexpr float kTwoPi = 6.28318530718f;
        const float angle = ux * kTwoPi;
        float r = radius;
        if (dist == ParticleSpawnDistribution::Surface) {
            r = radius;
        } else if (dist == ParticleSpawnDistribution::VolumeUniform) {
            // Area-uniform disk in XZ, height uniform along cylinder.
            r = radius * std::sqrt(uz);
        } else {
            // Volume: center-biased radial (legacy-ish fill).
            r = radius * uz;
        }
        const float y = (uy * 2.0f - 1.0f) * half_h;
        return glm::vec3(std::cos(angle) * r, y, std::sin(angle) * r);
    }
    case ParticleSpawnShape::Cone:
    default:
        // Cone/Point birth at origin; cone angle drives velocity only.
        return glm::vec3(0.0f);
    }
}

} // namespace

void ParticleWorld::enableGpuCompute(const VulkanContext& context) {
    try {
        gpu_sim_ = std::make_unique<ParticleGpuSim>(context);
        if (!gpu_sim_->ready()) {
            gpu_sim_.reset();
        }
    } catch (...) {
        gpu_sim_.reset();
    }
}

void ParticleWorld::clear() {
    emitters_.clear();
    particles_.clear();
    free_list_.clear();
    global_step_ = 0;
    sim_time_seconds_ = 0.0;
    pending_gpu_steps_ = 0;
    pending_gpu_dt_ = 0.0f;
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
    cullOrphanParticles();
}

void ParticleWorld::cullOrphanParticles() {
    bool any = false;
    for (std::size_t i = 0; i < particles_.size(); ++i) {
        Particle& p = particles_[i];
        if (!p.alive) {
            continue;
        }
        if (findEmitterRuntime(p.emitter_node) != nullptr) {
            continue;
        }
        p.alive = false;
        p.trail_enabled = false;
        p.trail_count = 0;
        free_list_.push_back(i);
        any = true;
    }
    if (any) {
        recountAlive();
    }
}

void ParticleWorld::applyEmitterVisualsToParticle(Particle& p, const ParticleEmitter& desc) {
    p.trail_enabled = desc.trail_enabled;
    p.trail_max_segments =
        static_cast<std::uint8_t>(std::clamp(static_cast<int>(desc.trail_segments), 2, 24));
    p.trail_sample_distance = std::max(0.005f, desc.trail_sample_distance);
    p.trail_width = std::max(0.0f, desc.trail_width);
    p.trail_width_end = std::max(0.0f, desc.trail_width_end);
    p.trail_alpha = std::clamp(desc.trail_alpha, 0.0f, 1.0f);
    p.trail_lifetime = std::max(0.05f, desc.trail_lifetime);
    if (!p.trail_enabled) {
        p.trail_count = 0;
        p.trail_dist_accum = 0.0f;
        p.trail_has_sample = false;
    } else if (p.trail_count > p.trail_max_segments) {
        // Drop oldest samples when segments shrinks mid-flight.
        const std::uint8_t drop =
            static_cast<std::uint8_t>(p.trail_count - p.trail_max_segments);
        for (std::uint8_t ti = 0; ti < p.trail_max_segments; ++ti) {
            p.trail_pos[ti] = p.trail_pos[ti + drop];
        }
        p.trail_count = p.trail_max_segments;
    }
    p.sprite_aspect = desc.sprite_aspect;
    p.velocity_stretch = desc.velocity_stretch;
    p.mesh = desc.mod_sprite ? desc.render_mesh : ParticleRenderMesh::Sphere;
    p.unlit = desc.unlit;
    p.additive = desc.additive;
    p.world_space = desc.world_space;
}

void ParticleWorld::applyAuthoringToLiving(Scene& scene) {
    syncEmitters(scene);
    stats_.emitters = static_cast<std::uint32_t>(emitters_.size());
    for (EmitterRuntime& runtime : emitters_) {
        Node* node = scene.find(runtime.node_id);
        if (!node || !node->particle_emitter.has_value()) {
            continue;
        }
        const ParticleEmitter& desc = *node->particle_emitter;
        // Re-arm finished one-shots when authoring still wants emission.
        if (runtime.finished && desc.enabled &&
            (desc.looping || desc.duration_seconds <= 0.0f ||
             optionalBurstArmed(desc) || desc.rate_per_second > 0.0f)) {
            runtime.finished = false;
            if (desc.looping || desc.duration_seconds <= 0.0f) {
                runtime.burst_fired = false;
                runtime.timed_bursts_fired = 0;
            }
        }
    }
    for (Particle& p : particles_) {
        if (!p.alive) {
            continue;
        }
        Node* node = scene.find(p.emitter_node);
        if (!node || !node->particle_emitter.has_value()) {
            p.alive = false;
            p.trail_enabled = false;
            p.trail_count = 0;
            continue;
        }
        applyEmitterVisualsToParticle(p, *node->particle_emitter);
    }
    recountAlive();
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

    std::uint32_t spawned = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (runtime.alive_count >= desc.max_particles) {
            break;
        }
        const std::uint64_t emit_index = runtime.next_emit_index++;
        const std::size_t slot = allocateParticle();
        Particle& p = particles_[slot];
        p = Particle{};
        p.alive = true;
        ++spawned;
        p.emitter_node = runtime.node_id;
        p.emit_index = emit_index;
        p.age = 0.0f;
        if (desc.mod_lifetime) {
            p.lifetime = std::max(0.001f, particleRandomRange(seed, runtime.node_id, emit_index,
                                                              kChanLifetime, desc.lifetime_min,
                                                              desc.lifetime_max));
        } else {
            p.lifetime = 1.0f;
        }
        if (desc.mod_size) {
            p.size0 = particleRandomRange(seed, runtime.node_id, emit_index, kChanSize, desc.size_min,
                                          desc.size_max);
        } else {
            p.size0 = 0.08f;
        }
        p.sprite_aspect = desc.sprite_aspect;
        p.velocity_stretch = desc.velocity_stretch;
        p.mesh = resolveParticleHeadMesh(desc);
        p.unlit = desc.unlit;
        p.additive = desc.additive;
        p.world_space = desc.world_space;
        if (desc.mod_size_over_life) {
            p.size_curve = authored.size_over_life;
        } else {
            p.size_curve = ParticleCurve1{};
            p.size_curve.count = 0;
        }
        if (desc.mod_color_over_life) {
            p.color_curve = authored.color_over_life;
        } else if (desc.mod_color) {
            p.color_curve = ParticleCurveColor::linear(desc.color_start, desc.color_start);
        } else {
            p.color_curve = ParticleCurveColor::linear(glm::vec4(1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
        }
        // Gentle palette: gradual yellow → green → blue (no rainbow / red pops).
        {
            const float u =
                particleRandom01(seed, runtime.node_id, emit_index, kChanColor);
            if (!desc.mod_color) {
                p.color_tint = glm::vec3(1.0f);
            } else {
                constexpr glm::vec3 kYellow(1.0f, 0.82f, 0.22f);
                constexpr glm::vec3 kGreen(0.35f, 0.95f, 0.45f);
                constexpr glm::vec3 kBlue(0.32f, 0.70f, 1.0f);
                if (u < 0.5f) {
                    const float t = u * 2.0f;
                    p.color_tint = glm::mix(kYellow, kGreen, t);
                } else {
                    const float t = (u - 0.5f) * 2.0f;
                    p.color_tint = glm::mix(kGreen, kBlue, t);
                }
            }
        }
        p.trail_enabled = desc.trail_enabled;
        p.trail_max_segments =
            static_cast<std::uint8_t>(std::clamp(static_cast<int>(desc.trail_segments), 2, 24));
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
        if (desc.mod_shape) {
            // Offset only for Sphere/Box/Ring — do NOT steal velocity direction from shape.
            local_offset = sampleSpawnLocalOffset(desc, seed, runtime.node_id, emit_index);
            if (desc.spawn_shape == ParticleSpawnShape::Box ||
                desc.spawn_shape == ParticleSpawnShape::Ring ||
                desc.spawn_shape == ParticleSpawnShape::Cone) {
                direction =
                    coneDirection(world, desc.cone_angle_degrees, seed, runtime.node_id, emit_index);
            }
        }

        if (desc.mod_velocity) {
            switch (desc.velocity_mode) {
            case ParticleVelocityMode::FromPoint: {
                const glm::vec3 world_pos = origin + glm::mat3(world) * local_offset;
                const glm::vec3 radial = world_pos - origin;
                if (glm::dot(radial, radial) > 1e-8f) {
                    direction = glm::normalize(radial);
                }
                break;
            }
            case ParticleVelocityMode::HorizontalPlane: {
                // Drift on the floor plane (cylinder footprint) — never birth with +Y.
                const float u = particleRandom01(seed, runtime.node_id, emit_index, kChanDirX);
                const float v = particleRandom01(seed, runtime.node_id, emit_index, kChanDirY);
                const float phi = v * glm::two_pi<float>();
                const float radial_mix = 0.35f + 0.65f * u;
                glm::vec3 planar(std::cos(phi) * radial_mix, 0.0f, std::sin(phi) * radial_mix);
                // Prefer tangential swirl around the emitter so the ring reads as a cylinder.
                const glm::vec3 world_pos = origin + glm::mat3(world) * local_offset;
                glm::vec3 radial = world_pos - origin;
                radial.y = 0.0f;
                if (glm::dot(radial, radial) > 1e-8f) {
                    radial = glm::normalize(radial);
                    const glm::vec3 tangent =
                        glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), radial));
                    planar = glm::normalize(tangent * (0.75f + 0.25f * u) +
                                            radial * (0.35f * (1.0f - u)));
                } else if (glm::dot(planar, planar) > 1e-8f) {
                    planar = glm::normalize(planar);
                } else {
                    planar = glm::vec3(1.0f, 0.0f, 0.0f);
                }
                direction = planar;
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
        }

        const float speed =
            desc.mod_velocity
                ? particleRandomRange(seed, runtime.node_id, emit_index, kChanSpeed, desc.speed_min,
                                      desc.speed_max)
                : 0.0f;
        glm::vec3 birth_vel = direction * speed;
        if (desc.mod_velocity && desc.inherit_velocity > 0.0f) {
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
    runtime.spawned_window += spawned;
}

ParticleWorld::EmitterLiveStats ParticleWorld::emitterLiveStats(NodeId emitter_node,
                                                                const Scene& scene) const {
    EmitterLiveStats out{};
    if (emitter_node == 0) {
        return out;
    }
    const EmitterRuntime* runtime = nullptr;
    for (const EmitterRuntime& er : emitters_) {
        if (er.node_id == emitter_node) {
            runtime = &er;
            break;
        }
    }
    if (!runtime) {
        return out;
    }
    out.found = true;
    out.alive = runtime->alive_count;
    out.measured_spawn_rate = runtime->measured_spawn_rate;
    if (const Node* node = scene.find(emitter_node); node && node->particle_emitter.has_value()) {
        out.authored_rate = node->particle_emitter->rate_per_second;
    }
    return out;
}

void ParticleWorld::stepOnce(Scene& scene, float dt, bool update_particles) {
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

        // Visible=off (incl. ancestors) stops spawn; Hidden-in-Game still emits in the editor.
        if (!desc.enabled || runtime.finished || !isNodeDrawnInView(scene, *node, false)) {
            continue;
        }

        runtime.age_seconds += static_cast<double>(dt);
        if (runtime.age_seconds < static_cast<double>(desc.start_delay_seconds)) {
            continue;
        }

        const double local_age = runtime.age_seconds - static_cast<double>(desc.start_delay_seconds);

        if (optionalBurstArmed(desc)) {
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

        if (!runtime.finished && desc.mod_spawn_rate && desc.rate_per_second > 0.0f) {
            runtime.emit_accumulator += static_cast<double>(desc.rate_per_second) * static_cast<double>(dt);
            const std::uint32_t to_spawn = static_cast<std::uint32_t>(runtime.emit_accumulator);
            if (to_spawn > 0) {
                runtime.emit_accumulator -= static_cast<double>(to_spawn);
                spawnFromEmitter(scene, runtime, desc, *node, to_spawn);
            }
        }

        // Tugela spawn-by-distance (Emitter Update).
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

        // Short-window measured spawn rate for inspector / hitch diagnosis.
        runtime.window_seconds += dt;
        if (runtime.window_seconds >= 0.25f) {
            runtime.measured_spawn_rate =
                static_cast<float>(runtime.spawned_window) / runtime.window_seconds;
            runtime.spawned_window = 0;
            runtime.window_seconds = 0.0f;
        }
    }

    if (update_particles) {
        updateParticlesCpu(scene, dt);
    }

    ++global_step_;
    sim_time_seconds_ += static_cast<double>(dt);
}

void ParticleWorld::updateParticlesCpu(Scene& scene, float dt) {
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
        if (!node || !node->particle_emitter.has_value() ||
            findEmitterRuntime(p.emitter_node) == nullptr) {
            p.alive = false;
            p.trail_enabled = false;
            p.trail_count = 0;
            free_list_.push_back(i);
            continue;
        }
        const ParticleEmitter& desc = *node->particle_emitter;
        // Live-apply trail / sprite authoring so inspector edits stick mid-flight.
        applyEmitterVisualsToParticle(p, desc);

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

        if (p.trail_enabled) {
            auto push_trail_sample = [&](const glm::vec3& sample_pos) {
                const std::uint8_t max_seg = std::min(p.trail_max_segments, kMaxTrailPoints);
                if (p.trail_count < max_seg) {
                    p.trail_pos[p.trail_count++] = sample_pos;
                } else {
                    for (std::uint8_t ti = 1; ti < max_seg; ++ti) {
                        p.trail_pos[ti - 1] = p.trail_pos[ti];
                    }
                    p.trail_pos[max_seg - 1] = sample_pos;
                    p.trail_count = max_seg;
                }
            };

            if (!p.trail_has_sample) {
                p.trail_last_sample = world_pos;
                p.trail_has_sample = true;
                p.trail_pos[0] = world_pos;
                p.trail_count = 1;
            } else {
                const glm::vec3 prev = p.trail_last_sample;
                const float moved = glm::length(world_pos - prev);
                p.trail_dist_accum += moved;
                // Honor authored sample distance (apply path clamps to ≥0.005m).
                const float sample_dist = std::max(0.005f, p.trail_sample_distance);
                const float early_dist =
                    p.trail_count < 6 ? std::min(sample_dist, 0.012f) : sample_dist;
                float sampled = 0.0f;
                while (p.trail_dist_accum >= early_dist) {
                    p.trail_dist_accum -= early_dist;
                    sampled += early_dist;
                    const float u =
                        (moved > 1e-6f) ? std::clamp(sampled / moved, 0.0f, 1.0f) : 1.0f;
                    push_trail_sample(glm::mix(prev, world_pos, u));
                }
                p.trail_last_sample = world_pos;
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
}

void ParticleWorld::simulate(Scene& scene, std::uint64_t steps, float dt) {
    stats_.steps = static_cast<std::uint32_t>(steps);
    const bool defer_update = gpuComputeEnabled();
    for (std::uint64_t i = 0; i < steps; ++i) {
        stepOnce(scene, dt, !defer_update);
    }
    if (defer_update && steps > 0) {
        pending_gpu_steps_ += static_cast<std::uint32_t>(steps);
        pending_gpu_dt_ = dt;
    }
    if (steps == 0) {
        syncEmitters(scene);
        stats_.emitters = static_cast<std::uint32_t>(emitters_.size());
        recountAlive();
    }
}

void ParticleWorld::flushGpuUpdates(VkCommandBuffer cmd, Scene& scene) {
    // Hybrid path always needs host readback for trails / CPU instance collect.
    // Use a persistent fence submit (not per-frame pool create/destroy).
    if (!gpuComputeEnabled() || pending_gpu_steps_ == 0) {
        return;
    }

    std::vector<GpuParticle> gpu = packGpuParticles(scene);
    if (gpu.empty()) {
        pending_gpu_steps_ = 0;
        recountAlive();
        return;
    }

    const float start_time = static_cast<float>(sim_time_seconds_) -
                             static_cast<float>(pending_gpu_steps_) * pending_gpu_dt_;
    const std::uint32_t count = static_cast<std::uint32_t>(gpu.size());
    const std::uint32_t steps = pending_gpu_steps_;
    const float dt = pending_gpu_dt_;
    gpu_sim_->upload(gpu.data(), count);
    gpu_sim_->flushImmediateAndWait(count, steps, dt, start_time);
    gpu_sim_->download(gpu.data(), count);
    unpackGpuParticles(gpu);
    updateTrailsAfterGpu(scene, dt * static_cast<float>(steps));
    pending_gpu_steps_ = 0;
    recountAlive();
    (void)cmd;
}

std::vector<GpuParticle> ParticleWorld::packGpuParticles(Scene& scene) const {
    // Alive-only pack: free/dead slots used to ride along every frame and balloon
    // upload/download + compute dispatch cost into hitch-sized stalls.
    std::vector<GpuParticle> out;
    out.reserve(particles_.size());
    for (std::size_t host_index = 0; host_index < particles_.size(); ++host_index) {
        const Particle& p = particles_[host_index];
        if (!p.alive) {
            continue;
        }
        GpuParticle g{};
        g.pos_age = glm::vec4(p.position, p.age);
        g.vel_life = glm::vec4(p.velocity, p.lifetime);
        g.size_params = glm::vec4(p.size0, p.sprite_aspect, p.velocity_stretch, 0.0f);
        std::uint32_t flags = kGpuParticleAlive;

        ParticleEmitter desc = makeDefaultParticleEmitter();
        if (const Node* node = scene.find(p.emitter_node);
            node && node->particle_emitter.has_value()) {
            desc = *node->particle_emitter;
        }
        if (desc.force_gravity) {
            flags |= kGpuParticleGravity;
            g.gravity = glm::vec4(desc.gravity, 0.0f);
        }
        if (desc.force_accel) {
            flags |= kGpuParticleAccel;
            g.accel_drag = glm::vec4(desc.acceleration, desc.drag);
        }
        if (desc.force_drag) {
            flags |= kGpuParticleDrag;
            g.accel_drag.w = desc.drag;
        }
        if (desc.kill_plane) {
            flags |= kGpuParticleKillPlane;
            g.kill_plane = glm::vec4(desc.kill_plane_y, 0.0f, 0.0f, 0.0f);
        }
        // meta.w = host particle slot for remapping after compact upload.
        g.meta = glm::uvec4(flags, static_cast<std::uint32_t>(p.emitter_node),
                            static_cast<std::uint32_t>(p.emit_index & 0xffffffffu),
                            static_cast<std::uint32_t>(host_index));
        out.push_back(g);
    }
    return out;
}

void ParticleWorld::unpackGpuParticles(const std::vector<GpuParticle>& gpu) {
    for (const GpuParticle& g : gpu) {
        const std::size_t host_index = static_cast<std::size_t>(g.meta.w);
        if (host_index >= particles_.size()) {
            continue;
        }
        Particle& p = particles_[host_index];
        p.position = glm::vec3(g.pos_age);
        p.age = g.pos_age.w;
        p.velocity = glm::vec3(g.vel_life);
        p.lifetime = g.vel_life.w;
        p.alive = (g.meta.x & kGpuParticleAlive) != 0u;
    }
    free_list_.clear();
    for (std::size_t i = 0; i < particles_.size(); ++i) {
        if (!particles_[i].alive) {
            free_list_.push_back(i);
        }
    }
}

void ParticleWorld::updateTrailsAfterGpu(Scene& scene, float /*total_dt*/) {
    for (Particle& p : particles_) {
        if (!p.alive) {
            continue;
        }
        const Node* node = scene.find(p.emitter_node);
        if (!node || !node->particle_emitter.has_value()) {
            p.trail_enabled = false;
            p.trail_count = 0;
            continue;
        }
        applyEmitterVisualsToParticle(p, *node->particle_emitter);
        if (!p.trail_enabled) {
            continue;
        }
        glm::vec3 world_pos = p.position;
        if (!p.world_space) {
            world_pos = glm::vec3(node->world * glm::vec4(p.position, 1.0f));
        }
        auto push_trail_sample = [&](const glm::vec3& sample_pos) {
            const std::uint8_t max_seg = std::min(p.trail_max_segments, kMaxTrailPoints);
            if (p.trail_count < max_seg) {
                p.trail_pos[p.trail_count++] = sample_pos;
            } else {
                for (std::uint8_t ti = 1; ti < max_seg; ++ti) {
                    p.trail_pos[ti - 1] = p.trail_pos[ti];
                }
                p.trail_pos[max_seg - 1] = sample_pos;
                p.trail_count = max_seg;
            }
        };
        if (!p.trail_has_sample) {
            p.trail_last_sample = world_pos;
            p.trail_has_sample = true;
            p.trail_pos[0] = world_pos;
            p.trail_count = 1;
        } else {
            const glm::vec3 prev = p.trail_last_sample;
            const float moved = glm::length(world_pos - prev);
            p.trail_dist_accum += moved;
            // Honor authored sample distance (same floor as applyEmitterVisualsToParticle).
            const float sample_dist = std::max(0.005f, p.trail_sample_distance);
            float sampled = 0.0f;
            while (p.trail_dist_accum >= sample_dist) {
                p.trail_dist_accum -= sample_dist;
                sampled += sample_dist;
                const float u = (moved > 1e-6f) ? std::clamp(sampled / moved, 0.0f, 1.0f) : 1.0f;
                push_trail_sample(glm::mix(prev, world_pos, u));
            }
            p.trail_last_sample = world_pos;
            if (p.trail_count > 0) {
                p.trail_pos[p.trail_count - 1] = world_pos;
            }
        }
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
                                     const ParticleViewBasis& view, bool game_output) const {
    // Batches: mesh × unlit × additive (12) + sprite ribbon trails (4). Keeps GPU instancing cheap.
    out.clear();
    out.resize(16);
    auto set_batch = [&](std::size_t i, ParticleRenderMesh mesh, bool unlit, bool additive,
                         bool ribbon = false) {
        out[i] = {mesh, unlit, additive, ribbon, {}};
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
    // Soft-ribbon trail strips (same mesh, ribbon texture in SceneRuntime).
    set_batch(12, ParticleRenderMesh::Sprite, false, false, true);
    set_batch(13, ParticleRenderMesh::Sprite, true, false, true);
    set_batch(14, ParticleRenderMesh::Sprite, false, true, true);
    set_batch(15, ParticleRenderMesh::Sprite, true, true, true);

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
        // Same gate as meshes: Visible / Hidden-in-Game + ancestry. Selection never overrides.
        if (const Node* emitter = scene.find(p.emitter_node);
            !emitter || !isNodeDrawnInView(scene, *emitter, game_output)) {
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
        // Tint owns hue; curve owns energy/alpha. Additive self-glow on black stage.
        {
            const float energy =
                std::max(0.2f, (color.r + color.g + color.b) * (1.0f / 3.0f));
            color.r = std::min(p.color_tint.r * energy * 1.2f, 1.2f);
            color.g = std::min(p.color_tint.g * energy * 1.2f, 1.2f);
            color.b = std::min(p.color_tint.b * energy * 1.2f, 1.2f);
            // Additive: alpha gates emission strength (keep below firefly).
            color.a = std::clamp(color.a, 0.0f, 0.7f);
        }

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
            mesh_base = 8;
            break;
        case ParticleRenderMesh::None:
        default:
            mesh_base = 8;
            break;
        }
        const std::size_t batch_index = mesh_base + (p.additive ? 2u : 0u) + (p.unlit ? 1u : 0u);

        auto push_sprite = [&](const glm::mat4& model, const glm::vec4& tint,
                               std::size_t dest_batch) {
            GpuMeshInstance instance{};
            fillGpuMeshInstance(instance, model, glm::vec3(-0.5f), glm::vec3(0.5f));
            instance.color = tint;
            out[dest_batch].instances.push_back(instance);
        };

        // Trail Renderer: wake always BEHIND the head (trail-only emitters OK).
        // Live-read authoring from the emitter so inspector edits affect draw even if a
        // particle field lagged one frame. Round H.2 look-lock clamps used to swallow
        // width/alpha/length so settings looked broken despite applyAuthoringToLiving.
        {
            bool trail_on = p.trail_enabled;
            float trail_w_head = p.trail_width;
            float trail_w_tip = p.trail_width_end;
            float trail_a = p.trail_alpha;
            float trail_life = p.trail_lifetime;
            float trail_sample = p.trail_sample_distance;
            std::uint8_t trail_segs = p.trail_max_segments;
            if (const Node* emitter_node = scene.find(p.emitter_node);
                emitter_node && emitter_node->particle_emitter.has_value()) {
                const ParticleEmitter& pe = *emitter_node->particle_emitter;
                trail_on = pe.trail_enabled;
                trail_w_head = pe.trail_width;
                trail_w_tip = pe.trail_width_end;
                trail_a = pe.trail_alpha;
                trail_life = pe.trail_lifetime;
                trail_sample = pe.trail_sample_distance;
                trail_segs = static_cast<std::uint8_t>(
                    std::clamp(static_cast<int>(pe.trail_segments), 2, 24));
            }

            if (trail_on && p.trail_count >= 2) {
                const float head_w = std::max(size, 0.04f);
                const std::size_t trail_batch =
                    12u + (p.additive ? 2u : 0u) + (p.unlit ? 1u : 0u);

                const glm::vec3 head = world_pos;
                const glm::vec3 oldest = p.trail_pos[0];
                auto project_plane = [&](glm::vec3 v) {
                    return v - cam_forward * glm::dot(v, cam_forward);
                };

                glm::vec3 travel = p.velocity;
                if (glm::dot(travel, travel) < 1e-8f) {
                    travel = head - oldest;
                }
                if (glm::dot(travel, travel) < 1e-8f) {
                    travel = cam_up;
                }
                travel = glm::normalize(travel);

                // Length: real history span, capped by authored sample budget
                // (segments × sample distance) and optional lifetime×speed feel.
                float wake_len = glm::length(head - oldest);
                const float sample_budget =
                    static_cast<float>(std::max(1, static_cast<int>(trail_segs) - 1)) *
                    std::max(0.005f, trail_sample);
                const float speed = glm::length(p.velocity);
                const float life_budget =
                    std::max(0.05f, trail_life) * std::max(speed, head_w * 0.5f);
                // Prefer history; lifetime feel can shorten. Budget grows with Segments /
                // Sample distance so those knobs visibly change ribbon length.
                const float max_len = std::max(sample_budget, head_w * 2.0f);
                wake_len = std::min(wake_len, max_len);
                wake_len = std::min(wake_len, std::max(life_budget, head_w * 0.35f));
                wake_len = std::max(wake_len, 0.0f);
                const float tip_span = wake_len;

                if (tip_span > head_w * 0.08f) {
                    glm::vec3 move_proj = project_plane(travel);
                    const float move_len = glm::length(move_proj);
                    const glm::vec3 move_dir =
                        (move_len > 1e-4f) ? (move_proj / move_len) : cam_up;

                    // Vertical Soft Circles: lock tip→head to ±cam_up so tip cannot flip.
                    const float rise = glm::dot(move_dir, cam_up);
                    glm::vec3 along = (std::abs(rise) >= 0.4f)
                                         ? ((rise >= 0.0f) ? cam_up : -cam_up)
                                         : move_dir;

                    glm::vec3 across = glm::cross(cam_forward, along);
                    const float across_len = glm::length(across);
                    if (across_len < 1e-4f) {
                        across = cam_right;
                    } else {
                        across /= across_len;
                    }

                    // Width: fractions of head size — no Round H.2 0.55–0.85 lock.
                    const float w_head =
                        head_w * std::clamp(std::max(0.0f, trail_w_head), 0.0f, 4.0f);
                    const float w_tip =
                        head_w * std::clamp(std::max(0.0f, trail_w_tip), 0.0f, 4.0f);

                    // Age fade from trail lifetime (0 at age≥lifetime — fully gone).
                    const float trail_age_fade = std::clamp(
                        1.0f - (p.age / std::max(trail_life, 0.05f)), 0.0f, 1.0f);
                    glm::vec4 trail_color = color;
                    // Alpha 0 must hide the wake (no Round H.2 floor of 0.22). Soft max
                    // keeps wakes under the head so tips do not bloom into a second disc.
                    trail_color.a = std::clamp(
                        color.a * std::clamp(trail_a, 0.0f, 1.0f) * trail_age_fade * 0.85f, 0.0f,
                        0.62f);
                    if (trail_color.a <= 1e-4f || (w_head <= 1e-5f && w_tip <= 1e-5f)) {
                        // Disabled via alpha/width — skip draw (re-enable restores next frame).
                    } else {
                        // Tip strictly opposite along from head (behind travel).
                        const glm::vec3 tip_end = head - along * tip_span;

                        // Single soft-wedge ribbon tip→head. Neck width = Width (head).
                        // Tip width scales the tip half via a second thinner overlay so both
                        // knobs read without Round H.2 locking them into ~0.7×head.
                        const glm::vec3 mid = tip_end + along * (tip_span * 0.5f);

                        auto push_wedge = [&](const glm::vec3& a, const glm::vec3& b, float width,
                                              float alpha_mul) {
                            if (width <= 1e-5f) {
                                return;
                            }
                            const float span = glm::length(b - a);
                            if (span <= 1e-5f) {
                                return;
                            }
                            glm::vec4 tint = trail_color;
                            tint.a = std::clamp(tint.a * alpha_mul, 0.0f, 0.85f);
                            if (tint.a <= 1e-4f) {
                                return;
                            }
                            glm::mat4 model(1.0f);
                            model[0] = glm::vec4(across * width, 0.0f);
                            // +Y → headward (V=1 neck); -Y → tipward (V=0 point).
                            model[1] = glm::vec4(along * span, 0.0f);
                            model[2] = glm::vec4(cam_forward, 0.0f);
                            model[3] = glm::vec4((a + b) * 0.5f, 1.0f);
                            push_sprite(model, tint, trail_batch);
                        };

                        // Full wake at head width (wedge texture supplies silhouette taper).
                        push_wedge(tip_end, head, std::max(w_head, 1e-4f), 1.0f);
                        // Tip accent: when tip width differs, add/replace tip half so the
                        // Width (tip) slider is visible (thinner tip = darker tip half).
                        if (std::abs(w_tip - w_head) > head_w * 0.02f) {
                            const float tip_alpha =
                                std::clamp(w_tip / std::max(w_head, 1e-4f), 0.0f, 1.25f);
                            push_wedge(tip_end, mid, std::max(w_tip, 1e-4f), tip_alpha * 0.65f);
                        }
                    }
                }
            }
        }

        // Primary head — after the ribbon so sprites seal the trail join. None = invisible head.
        if (p.mesh == ParticleRenderMesh::None) {
            continue;
        }
        glm::mat4 model(1.0f);
        if (p.mesh == ParticleRenderMesh::Sprite) {
            const float speed = glm::length(p.velocity);
            float hx = size;
            float hy = size;
            if (p.velocity_stretch > 1e-4f && speed > 1e-4f) {
                const float aspect = std::max(p.sprite_aspect, 0.05f);
                hx = size * aspect;
                hy = size * (1.0f + speed * p.velocity_stretch);
            }
            // Controlled emission halo (same tint) — soft falloff, not a huge blur plate.
            {
                glm::vec4 glow = color;
                glow.a = std::clamp(glow.a * 0.28f, 0.1f, 0.22f);
                glm::mat4 glow_model(1.0f);
                glow_model[0] = glm::vec4(cam_right * hx * 1.35f, 0.0f);
                glow_model[1] = glm::vec4(cam_up * hy * 1.35f, 0.0f);
                glow_model[2] = glm::vec4(cam_forward, 0.0f);
                glow_model[3] = glm::vec4(world_pos, 1.0f);
                push_sprite(glow_model, glow, batch_index);
            }
            // Bright core disc — seals trail join; oversized vs tip so the head always leads.
            glm::vec4 head_color = color;
            head_color.a = std::min(std::max(head_color.a, 0.68f), 0.9f);
            hx *= 1.22f;
            hy *= 1.22f;
            model[0] = glm::vec4(cam_right * hx, 0.0f);
            model[1] = glm::vec4(cam_up * hy, 0.0f);
            model[2] = glm::vec4(cam_forward, 0.0f);
            model[3] = glm::vec4(world_pos, 1.0f);
            push_sprite(model, head_color, batch_index);
        } else {
            model = glm::translate(glm::mat4(1.0f), world_pos);
            model = glm::scale(model, glm::vec3(size));
            push_sprite(model, color, batch_index);
        }
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
