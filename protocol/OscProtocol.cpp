#include "protocol/OscProtocol.hpp"

#include "protocol/OscParser.hpp"
#include "scene/ParamBinding.hpp"
#include "scene/ScenePath.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <memory>

namespace genesis {

namespace {

constexpr std::size_t kMaxQueuedParticleEmits = 4096;
constexpr std::size_t kMaxQueuedRoomPerson = 2048;

std::optional<NodeId> findSceneRoot(Scene& scene) {
    for (NodeId root_id : scene.roots) {
        if (const Node* root = scene.find(root_id); root && root->name == "Scene") {
            return root_id;
        }
    }
    return std::nullopt;
}

NodeId ensureEmptyPath(Scene& scene, const std::string& path) {
    if (std::optional<NodeId> existing = findNodeByPath(scene, path)) {
        return *existing;
    }
    std::optional<NodeId> parent;
    std::string leaf = path;
    const std::size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        parent = ensureEmptyPath(scene, path.substr(0, slash));
        leaf = path.substr(slash + 1);
    } else if (path == "Scene") {
        if (std::optional<NodeId> root = findSceneRoot(scene)) {
            return *root;
        }
    }
    return scene.createEmptyNode(leaf.empty() ? "Empty" : leaf, parent);
}

NodeId ensureOscEmitterNode(Scene& scene, const std::string& emitter_path,
                            const std::function<ParticleEmitter()>& make_emitter,
                            const char* default_leaf) {
    if (std::optional<NodeId> existing = findNodeByPath(scene, emitter_path)) {
        if (Node* node = scene.find(*existing)) {
            if (!node->particle_emitter.has_value()) {
                node->has_particle_emitter = true;
                node->particle_emitter = make_emitter();
            }
            return *existing;
        }
    }

    std::optional<NodeId> parent;
    std::string leaf = emitter_path;
    const std::size_t slash = emitter_path.find_last_of('/');
    if (slash != std::string::npos) {
        const std::string parent_path = emitter_path.substr(0, slash);
        leaf = emitter_path.substr(slash + 1);
        parent = ensureEmptyPath(scene, parent_path);
    } else {
        parent = findSceneRoot(scene);
    }

    return scene.createParticleEmitterNode(leaf.empty() ? default_leaf : leaf, parent, make_emitter());
}

NodeId ensureOscParticleEmitterNode(Scene& scene, const std::string& emitter_path) {
    return ensureOscEmitterNode(scene, emitter_path, [] { return makeOscParticleEmitter(); },
                                "OSC Tracker");
}

} // namespace

void OscProtocol::setSettings(const Settings& settings) {
    const bool endpoint_changed = settings.bind_ip != settings_.bind_ip || settings.port != settings_.port;
    settings_ = settings;
    if (endpoint_changed) {
        settings_dirty_ = true;
    }
    if (!settings_.listening && receiver_.isListening()) {
        stopListening();
    }
}

bool OscProtocol::startListening(std::string* out_error) {
    stopListening();
    if (!receiver_.start(settings_.bind_ip, settings_.port, out_error)) {
        if (out_error) {
            last_error_ = *out_error;
        }
        settings_.listening = false;
        return false;
    }
    settings_.listening = true;
    settings_dirty_ = false;
    last_error_.clear();
    return true;
}

void OscProtocol::stopListening() {
    receiver_.stop();
    settings_.listening = false;
}

void OscProtocol::poll(double now_seconds) {
    last_poll_seconds_ = now_seconds;
    if (settings_dirty_ && settings_.listening) {
        startListening(&last_error_);
    }

    if (!receiver_.isListening()) {
        return;
    }

    std::vector<std::vector<std::uint8_t>> packets;
    const int count = receiver_.poll(packets);
    if (count <= 0) {
        return;
    }

    total_packets_ += static_cast<std::uint64_t>(count);
    for (const std::vector<std::uint8_t>& packet : packets) {
        std::string error;
        const std::vector<OscMessage> messages = parseOscPackets(packet, &error);
        if (messages.empty()) {
            if (!error.empty()) {
                last_error_ = error;
                ++parse_errors_;
            }
            continue;
        }

        total_messages_ += messages.size();
        last_error_.clear();
        for (const OscMessage& message : messages) {
            ingestMessage(message, now_seconds);
        }
    }
}

void OscProtocol::ingestDecodedMessage(const OscMessage& message, double now_seconds) {
    last_poll_seconds_ = now_seconds;
    ingestMessage(message, now_seconds);
}

void OscProtocol::ingestMessage(const OscMessage& message, double now_seconds) {
    storeLastValues(message);
    ingestParticleEmit(message);
    ingestRoomPerson(message, now_seconds);
    if (settings_.learn_enabled) {
        updateLearned(message, now_seconds);
    }
}

void OscProtocol::ingestParticleEmit(const OscMessage& message) {
    OscParticleEmitEvent event{};
    if (!tryParseOscParticleEmit(message, particle_emit_, event)) {
        return;
    }
    event.sequence = next_particle_sequence_++;
    particle_events_.push_back(event);
    ++total_particle_emits_;
    if (particle_events_.size() > kMaxQueuedParticleEmits) {
        const std::size_t drop = particle_events_.size() - kMaxQueuedParticleEmits;
        particle_events_.erase(particle_events_.begin(),
                               particle_events_.begin() + static_cast<std::ptrdiff_t>(drop));
    }
}

void OscProtocol::ingestRoomPerson(const OscMessage& message, double now_seconds) {
    OscRoomPersonEvent event{};
    if (!tryParseOscRoomPerson(message, room_person_, event)) {
        return;
    }
    event.sequence = next_room_person_sequence_++;
    event.received_seconds = now_seconds;
    room_person_events_.push_back(event);
    ++total_room_person_updates_;
    if (room_person_events_.size() > kMaxQueuedRoomPerson) {
        const std::size_t drop = room_person_events_.size() - kMaxQueuedRoomPerson;
        room_person_events_.erase(room_person_events_.begin(),
                                  room_person_events_.begin() + static_cast<std::ptrdiff_t>(drop));
    }
}

void OscProtocol::storeLastValues(const OscMessage& message) {
    last_values_[message.address] = message.args;
}

void OscProtocol::updateLearned(const OscMessage& message, double now_seconds) {
    auto it = std::find_if(learned_.begin(), learned_.end(),
                           [&](const OscLearnedMessage& entry) { return entry.address == message.address; });
    if (it == learned_.end()) {
        OscLearnedMessage entry{};
        entry.address = message.address;
        entry.receive_count = 1;
        entry.last_seen_seconds = now_seconds;
        for (const OscArg& arg : message.args) {
            entry.arg_preview.push_back(oscArgToString(arg));
        }
        learned_.push_back(std::move(entry));
    } else {
        it->receive_count += 1;
        it->last_seen_seconds = now_seconds;
        it->arg_preview.clear();
        for (const OscArg& arg : message.args) {
            it->arg_preview.push_back(oscArgToString(arg));
        }
    }

    std::sort(learned_.begin(), learned_.end(),
              [](const OscLearnedMessage& a, const OscLearnedMessage& b) {
                  return a.last_seen_seconds > b.last_seen_seconds;
              });
}

void OscProtocol::applyBindings(SceneRuntime& runtime) {
    bool transform_dirty = false;
    for (const OscBinding& binding : bindings_) {
        const auto values_it = last_values_.find(binding.address);
        if (values_it == last_values_.end()) {
            continue;
        }
        const std::vector<OscArg>& args = values_it->second;
        if (binding.arg_index < 0 || binding.arg_index >= static_cast<int>(args.size())) {
            continue;
        }
        const float value = oscArgAsFloat(args[static_cast<std::size_t>(binding.arg_index)]);
        if (applySceneParam(runtime, binding.target, value, binding.node_path, binding.material_id)) {
            if (sceneParamAffectsTransforms(binding.target)) {
                transform_dirty = true;
            }
        }
    }
    if (transform_dirty) {
        runtime.updateTransforms();
    }
}

std::uint32_t OscProtocol::applyParticleEmits(SceneRuntime& runtime, std::uint64_t& consumer_cursor) {
    if (!particle_emit_.enabled || particle_events_.empty()) {
        return 0;
    }

    std::uint32_t spawned_total = 0;
    bool applied_any = false;
    for (const OscParticleEmitEvent& event : particle_events_) {
        if (event.sequence <= consumer_cursor) {
            continue;
        }
        const NodeId emitter_id =
            ensureOscParticleEmitterNode(runtime.scene(), particle_emit_.emitter_path);
        const glm::vec3 world = oscParticleWorldPosition(particle_emit_, event);

        if (particle_emit_.track_emitter_position) {
            if (Node* node = runtime.scene().find(emitter_id)) {
                node->local.position = world;
            }
        }
        runtime.updateTransforms();

        if (particle_emit_.burst_on_message) {
            spawned_total += runtime.particleWorld().emitBurstAtWorld(runtime.scene(), emitter_id, world,
                                                                     event.count);
        }
        consumer_cursor = event.sequence;
        applied_any = true;
    }

    if (applied_any) {
        runtime.updateTransforms();
    }
    return spawned_total;
}

void OscProtocol::clearParticleEmits() {
    particle_events_.clear();
}

void OscProtocol::clearRoomPersonEvents() {
    room_person_events_.clear();
}

std::uint32_t OscProtocol::applyRoomPerson(SceneRuntime& runtime, std::uint64_t& consumer_cursor) {
    if (!room_person_.enabled) {
        return 0;
    }

    const std::size_t slot_count = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::max(1u, room_person_.max_persons)), 1u,
        kRoomPersonSlotCapacity);
    const double now = last_poll_seconds_;

    // Ensure the full pre-created pool exists (constant load; idle pairs at origin).
    ensureEmptyPath(runtime.scene(), room_person_.person_pool_path);
    for (std::size_t slot = 0; slot < slot_count; ++slot) {
        ensureOscEmitterNode(runtime.scene(), roomPersonFeetPath(room_person_, slot),
                             [] { return makeRoomPersonFeetEmitter(); }, "Feet");
        ensureOscEmitterNode(runtime.scene(), roomPersonWallPath(room_person_, slot),
                             [] { return makeRoomWallTraceEmitter(); }, "Wall");
    }

    // Optional single wall plane: Plane mesh faces +Y; world axes from node transform.
    // Only the node at wall_plane_path is used — no multi-plane fan / clutter.
    std::optional<WallPlane> wall_plane;
    if (room_person_.wall_ray_mode != WallRaySpawnMode::FixedDistance &&
        !room_person_.wall_plane_path.empty()) {
        runtime.updateTransforms();
        if (const std::optional<NodeId> plane_id =
                findNodeByPath(runtime.scene(), room_person_.wall_plane_path)) {
            if (const Node* plane_node = runtime.scene().find(*plane_id); plane_node &&
                plane_node->has_mesh) {
                WallPlane plane{};
                plane.point = glm::vec3(plane_node->world[3]);
                const glm::mat3 rot(plane_node->world);
                const glm::vec3 n = rot * glm::vec3(0.0f, 1.0f, 0.0f);
                const glm::vec3 tu = rot * glm::vec3(1.0f, 0.0f, 0.0f);
                const glm::vec3 tv = rot * glm::vec3(0.0f, 0.0f, 1.0f);
                const float nlen = glm::length(n);
                const float ulen = glm::length(tu);
                const float vlen = glm::length(tv);
                if (nlen > 1e-4f && ulen > 1e-4f && vlen > 1e-4f) {
                    plane.normal = n / nlen;
                    plane.tangent = tu / ulen;
                    plane.bitangent = tv / vlen;
                    // createPlaneMesh unit half-extent = 0.5; scale → world half-size.
                    plane.half_u = 0.5f * ulen;
                    plane.half_v = 0.5f * vlen;
                    wall_plane = plane;
                }
            }
        }
    }
    const WallPlane* plane_ptr = wall_plane ? &*wall_plane : nullptr;

    auto set_emitter_active = [&](NodeId id, bool active) {
        if (Node* node = runtime.scene().find(id); node && node->particle_emitter.has_value()) {
            node->particle_emitter->enabled = active;
        }
    };

    auto park_slot = [&](std::size_t slot) {
        const NodeId feet_id = ensureOscEmitterNode(
            runtime.scene(), roomPersonFeetPath(room_person_, slot),
            [] { return makeRoomPersonFeetEmitter(); }, "Feet");
        const NodeId wall_id = ensureOscEmitterNode(
            runtime.scene(), roomPersonWallPath(room_person_, slot),
            [] { return makeRoomWallTraceEmitter(); }, "Wall");
        if (Node* feet = runtime.scene().find(feet_id)) {
            feet->local.position = glm::vec3(0.0f);
        }
        if (Node* wall = runtime.scene().find(wall_id)) {
            wall->local.position = glm::vec3(0.0f);
        }
        // Keep systems allocated at origin, but stop spawning while idle so the
        // install doesn't build a particle tower at 0,0,0.
        set_emitter_active(feet_id, false);
        set_emitter_active(wall_id, false);
        room_person_slots_[slot] = OscRoomPersonSlot{};
        wall_ray_debug_[slot] = OscWallRayDebugSample{};
    };

    auto find_slot_for_id = [&](std::int32_t person_id) -> std::size_t {
        for (std::size_t slot = 0; slot < slot_count; ++slot) {
            if (room_person_slots_[slot].occupied &&
                room_person_slots_[slot].person_id == person_id) {
                return slot;
            }
        }
        for (std::size_t slot = 0; slot < slot_count; ++slot) {
            if (!room_person_slots_[slot].occupied) {
                return slot;
            }
        }
        // Steal the least-recently-seen occupied slot.
        std::size_t lru = 0;
        for (std::size_t slot = 1; slot < slot_count; ++slot) {
            if (room_person_slots_[slot].last_seen_seconds <
                room_person_slots_[lru].last_seen_seconds) {
                lru = slot;
            }
        }
        return lru;
    };

    std::uint32_t spawned_total = 0;
    bool applied_any = false;

    for (const OscRoomPersonEvent& event : room_person_events_) {
        if (event.sequence <= consumer_cursor) {
            continue;
        }

        const OscRoomPersonPose pose = oscRoomPersonPose(room_person_, event, plane_ptr);
        const std::size_t slot = find_slot_for_id(event.person_id);
        room_person_slots_[slot].occupied = true;
        room_person_slots_[slot].person_id = event.person_id;
        room_person_slots_[slot].last_seen_seconds =
            event.received_seconds > 0.0 ? event.received_seconds : now;

        const NodeId feet_id = ensureOscEmitterNode(
            runtime.scene(), roomPersonFeetPath(room_person_, slot),
            [] { return makeRoomPersonFeetEmitter(); }, "Feet");
        const NodeId wall_id = ensureOscEmitterNode(
            runtime.scene(), roomPersonWallPath(room_person_, slot),
            [] { return makeRoomWallTraceEmitter(); }, "Wall");

        if (room_person_.track_feet_emitter) {
            if (Node* node = runtime.scene().find(feet_id)) {
                node->local.position = pose.feet;
            }
        }
        if (room_person_.track_wall_emitter) {
            if (Node* node = runtime.scene().find(wall_id)) {
                node->local.position = pose.wall;
            }
        }
        set_emitter_active(feet_id, true);
        set_emitter_active(wall_id, true);

        OscWallRayDebugSample& dbg = wall_ray_debug_[slot];
        dbg.person_id = event.person_id;
        dbg.center = glm::vec3(pose.center.x, room_person_.wall_spawn_height, pose.center.z);
        dbg.person = pose.feet;
        dbg.wall = pose.wall;
        dbg.used_plane_hit = pose.used_plane_hit;
        dbg.active = true;

        const std::uint32_t scale =
            event.count > 0 ? event.count
                            : std::max(room_person_.feet_burst, room_person_.wall_burst);
        const float scale_mul =
            event.count > 0
                ? static_cast<float>(scale) /
                      static_cast<float>(std::max(1u, room_person_.feet_burst))
                : 1.0f;
        auto scaled_count = [&](std::uint32_t base) {
            const float c = static_cast<float>(base) * scale_mul;
            return static_cast<std::uint32_t>(
                std::clamp(std::lround(c), 0L, static_cast<long>(room_person_.max_burst)));
        };

        if (room_person_.burst_at_feet) {
            spawned_total += runtime.particleWorld().emitBurstAtWorld(
                runtime.scene(), feet_id, pose.feet, scaled_count(room_person_.feet_burst));
        }
        if (room_person_.burst_at_wall) {
            spawned_total += runtime.particleWorld().emitBurstAtWorld(
                runtime.scene(), wall_id, pose.wall, scaled_count(room_person_.wall_burst));
        }
        if (room_person_.burst_along_ray && room_person_.ray_samples > 0) {
            const NodeId ray_id = ensureOscEmitterNode(
                runtime.scene(), room_person_.ray_emitter_path,
                [] { return makeRoomCenterRayEmitter(); }, "Center Ray");
            const std::uint32_t each = scaled_count(room_person_.ray_burst_each);
            for (std::uint32_t s = 1; s <= room_person_.ray_samples; ++s) {
                const float u =
                    static_cast<float>(s) / static_cast<float>(room_person_.ray_samples + 1);
                const glm::vec3 p = glm::mix(pose.center, pose.wall, u);
                const glm::vec3 sample(p.x, glm::mix(pose.feet.y, pose.wall.y, u), p.z);
                spawned_total +=
                    runtime.particleWorld().emitBurstAtWorld(runtime.scene(), ray_id, sample, each);
            }
        }

        consumer_cursor = event.sequence;
        applied_any = true;
    }

    // Park idle IDs at world origin (keep emitters alive for constant load).
    for (std::size_t slot = 0; slot < slot_count; ++slot) {
        if (!room_person_slots_[slot].occupied) {
            park_slot(slot);
            continue;
        }
        if (room_person_.slot_idle_seconds > 0.0f &&
            (now - room_person_slots_[slot].last_seen_seconds) >=
                static_cast<double>(room_person_.slot_idle_seconds)) {
            park_slot(slot);
            applied_any = true;
        }
    }
    // Any slots beyond max_persons stay parked.
    for (std::size_t slot = slot_count; slot < kRoomPersonSlotCapacity; ++slot) {
        if (room_person_slots_[slot].occupied) {
            park_slot(slot);
            applied_any = true;
        }
    }

    if (applied_any) {
        runtime.updateTransforms();
    }
    return spawned_total;
}

void OscProtocol::clearLearned() {
    learned_.clear();
}

void OscProtocol::removeBinding(std::size_t index) {
    if (index < bindings_.size()) {
        bindings_.erase(bindings_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void OscProtocol::clearBindings() {
    bindings_.clear();
}

void OscProtocol::beginLink(const std::string& address, int arg_index) {
    pending_link_ = PendingLink{address, arg_index};
    pending_link_address_ = address;
    pending_link_arg_index_ = arg_index;
}

void OscProtocol::cancelLink() {
    pending_link_.reset();
    pending_link_address_.clear();
    pending_link_arg_index_ = 0;
}

bool OscProtocol::completeLink(OscParamTarget target, const std::string& node_path,
                               std::uint32_t material_id) {
    if (!pending_link_.has_value()) {
        return false;
    }

    OscBinding binding{};
    binding.address = pending_link_->address;
    binding.arg_index = pending_link_->arg_index;
    binding.target = target;
    binding.node_path = node_path;
    binding.material_id = material_id;

    auto existing = std::find_if(bindings_.begin(), bindings_.end(), [&](const OscBinding& b) {
        return b.address == binding.address && b.arg_index == binding.arg_index &&
               b.target == binding.target && b.node_path == binding.node_path &&
               b.material_id == binding.material_id;
    });
    if (existing == bindings_.end()) {
        bindings_.push_back(binding);
    } else {
        *existing = binding;
    }

    cancelLink();
    return true;
}

OscProtocol& sharedOscProtocol() {
    static std::unique_ptr<OscProtocol> instance = std::make_unique<OscProtocol>();
    return *instance;
}

} // namespace genesis
