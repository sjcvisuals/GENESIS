#include "protocol/OscProtocol.hpp"

#include "protocol/OscParser.hpp"
#include "scene/ScenePath.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace genesis {

namespace {

constexpr float kCentimetersPerUnit = 100.0f;

bool applyBindingTarget(SceneRuntime& runtime, const OscBinding& binding, float value) {
    Node* node = binding.node_path.empty() ? nullptr : runtime.findNodeByPath(binding.node_path);
    SceneMaterial* mat = nullptr;
    if (binding.material_id < runtime.materials().size()) {
        mat = runtime.material(binding.material_id);
    }

    auto setNodeRotationComponent = [&](int axis, float degrees) -> bool {
        if (!node) {
            return false;
        }
        const glm::vec3 euler = glm::degrees(glm::eulerAngles(node->local.rotation));
        glm::vec3 updated = euler;
        updated[axis] = degrees;
        const float x = glm::radians(updated.x);
        const float y = glm::radians(updated.y);
        const float z = glm::radians(updated.z);
        const glm::quat qx = glm::angleAxis(x, glm::vec3(1, 0, 0));
        const glm::quat qy = glm::angleAxis(y, glm::vec3(0, 1, 0));
        const glm::quat qz = glm::angleAxis(z, glm::vec3(0, 0, 1));
        node->local.rotation = glm::normalize(qy * qx * qz);
        return true;
    };

    switch (binding.target) {
    case OscParamTarget::NodePositionX:
        if (!node) return false;
        node->local.position.x = value / kCentimetersPerUnit;
        return true;
    case OscParamTarget::NodePositionY:
        if (!node) return false;
        node->local.position.y = value / kCentimetersPerUnit;
        return true;
    case OscParamTarget::NodePositionZ:
        if (!node) return false;
        node->local.position.z = value / kCentimetersPerUnit;
        return true;
    case OscParamTarget::NodeScaleX:
        if (!node) return false;
        node->local.scale.x = value / 100.0f;
        return true;
    case OscParamTarget::NodeScaleY:
        if (!node) return false;
        node->local.scale.y = value / 100.0f;
        return true;
    case OscParamTarget::NodeScaleZ:
        if (!node) return false;
        node->local.scale.z = value / 100.0f;
        return true;
    case OscParamTarget::NodeRotationX:
        return setNodeRotationComponent(0, value);
    case OscParamTarget::NodeRotationY:
        return setNodeRotationComponent(1, value);
    case OscParamTarget::NodeRotationZ:
        return setNodeRotationComponent(2, value);
    case OscParamTarget::NodeVisible:
        if (!node) return false;
        node->visible = value > 0.5f;
        return true;
    case OscParamTarget::MaterialBaseColorR:
        if (!mat) return false;
        mat->base_color.r = value;
        return true;
    case OscParamTarget::MaterialBaseColorG:
        if (!mat) return false;
        mat->base_color.g = value;
        return true;
    case OscParamTarget::MaterialBaseColorB:
        if (!mat) return false;
        mat->base_color.b = value;
        return true;
    case OscParamTarget::MaterialBaseColorA:
        if (!mat) return false;
        mat->base_color.a = value;
        return true;
    case OscParamTarget::MaterialMetallic:
        if (!mat) return false;
        mat->metallic = value;
        return true;
    case OscParamTarget::MaterialRoughness:
        if (!mat) return false;
        mat->roughness = value;
        return true;
    case OscParamTarget::MaterialEmissiveStrength:
        if (!mat) return false;
        mat->emissive_strength = value;
        return true;
    case OscParamTarget::LightEnabled:
        if (!node || !node->light.has_value()) return false;
        node->light->enabled = value > 0.5f;
        return true;
    case OscParamTarget::LightIntensity:
        if (!node || !node->light.has_value()) return false;
        node->light->intensity = value;
        return true;
    case OscParamTarget::LightColorR:
        if (!node || !node->light.has_value()) return false;
        node->light->color.r = value;
        return true;
    case OscParamTarget::LightColorG:
        if (!node || !node->light.has_value()) return false;
        node->light->color.g = value;
        return true;
    case OscParamTarget::LightColorB:
        if (!node || !node->light.has_value()) return false;
        node->light->color.b = value;
        return true;
    case OscParamTarget::ParticleRate:
        if (!node || !node->particle_emitter.has_value()) return false;
        node->particle_emitter->rate_per_second = std::max(value, 0.0f);
        return true;
    case OscParamTarget::CameraFov:
        if (!node || !node->camera.has_value()) return false;
        node->camera->fov_y_radians = glm::radians(std::clamp(value, 1.0f, 170.0f));
        return true;
    default:
        return false;
    }
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

void OscProtocol::ingestMessage(const OscMessage& message, double now_seconds) {
    storeLastValues(message);
    if (settings_.learn_enabled) {
        updateLearned(message, now_seconds);
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
        if (applyBindingTarget(runtime, binding, value)) {
            switch (binding.target) {
            case OscParamTarget::NodePositionX:
            case OscParamTarget::NodePositionY:
            case OscParamTarget::NodePositionZ:
            case OscParamTarget::NodeScaleX:
            case OscParamTarget::NodeScaleY:
            case OscParamTarget::NodeScaleZ:
            case OscParamTarget::NodeRotationX:
            case OscParamTarget::NodeRotationY:
            case OscParamTarget::NodeRotationZ:
                transform_dirty = true;
                break;
            default:
                break;
            }
        }
    }
    if (transform_dirty) {
        runtime.updateTransforms();
    }
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

} // namespace genesis
