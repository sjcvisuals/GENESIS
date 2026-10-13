#include "protocol/OscProtocol.hpp"

#include "protocol/OscParser.hpp"
#include "scene/ParamBinding.hpp"
#include "scene/ScenePath.hpp"

#include <algorithm>
#include <memory>

namespace genesis {

namespace {

constexpr std::size_t kMaxQueuedParticleEmits = 4096;

NodeId ensureOscParticleEmitterNode(Scene& scene, const std::string& emitter_path) {
    if (std::optional<NodeId> existing = findNodeByPath(scene, emitter_path)) {
        if (Node* node = scene.find(*existing)) {
            if (!node->particle_emitter.has_value()) {
                node->has_particle_emitter = true;
                node->particle_emitter = makeOscParticleEmitter();
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
        parent = findNodeByPath(scene, parent_path);
        if (!parent.has_value() && parent_path == "Scene" && !scene.roots.empty()) {
            // Prefer an existing root named Scene when present.
            for (NodeId root_id : scene.roots) {
                if (const Node* root = scene.find(root_id); root && root->name == "Scene") {
                    parent = root_id;
                    break;
                }
            }
        }
    }

    return scene.createParticleEmitterNode(leaf.empty() ? "OSC Particles" : leaf, parent,
                                           makeOscParticleEmitter());
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
    ingestMessage(message, now_seconds);
}

void OscProtocol::ingestMessage(const OscMessage& message, double now_seconds) {
    storeLastValues(message);
    ingestParticleEmit(message);
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
        const NodeId emitter_id = ensureOscParticleEmitterNode(runtime.scene(), particle_emit_.emitter_path);
        runtime.updateTransforms();
        const glm::vec3 world = oscParticleWorldPosition(particle_emit_, event);
        spawned_total += runtime.particleWorld().emitBurstAtWorld(runtime.scene(), emitter_id, world,
                                                                 event.count);
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
