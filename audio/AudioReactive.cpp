#include "audio/AudioReactive.hpp"

#include "protocol/OscTypes.hpp"
#include "scene/ParamBinding.hpp"

#include <algorithm>
#include <cmath>

namespace genesis {

std::string audioBindingSummary(const AudioBinding& binding) {
    std::string summary = binding.node_path.empty() ? "(scene)" : binding.node_path;
    summary += " · ";
    summary += audioFeatureChannelLabel(binding.channel);
    summary += " → ";
    summary += oscParamTargetLabel(binding.target);
    return summary;
}

void AudioReactive::tick(SceneRuntime& runtime, float dt_seconds) {
    if (!settings_.enabled) {
        return;
    }

    capture_.pump(analyser_, dt_seconds);
    analyser_.tick(dt_seconds);

    bool transform_dirty = false;
    const AudioFeatures& f = analyser_.features();
    for (const AudioBinding& binding : bindings_) {
        if (!binding.enabled) {
            continue;
        }
        const float feature = audioFeatureValue(f, binding.channel) * settings_.master_gain;
        const float mapped =
            std::clamp(binding.bias + binding.gain * feature, binding.min_value, binding.max_value);
        if (applySceneParam(runtime, binding.target, mapped, binding.node_path, binding.material_id)) {
            if (sceneParamAffectsTransforms(binding.target)) {
                transform_dirty = true;
            }
        }
    }
    if (transform_dirty) {
        runtime.updateTransforms();
    }
}

void AudioReactive::removeBinding(std::size_t index) {
    if (index < bindings_.size()) {
        bindings_.erase(bindings_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void AudioReactive::clearBindings() {
    bindings_.clear();
}

void AudioReactive::addBinding(const AudioBinding& binding) {
    auto existing = std::find_if(bindings_.begin(), bindings_.end(), [&](const AudioBinding& b) {
        return b.channel == binding.channel && b.target == binding.target &&
               b.node_path == binding.node_path && b.material_id == binding.material_id;
    });
    if (existing == bindings_.end()) {
        bindings_.push_back(binding);
    } else {
        *existing = binding;
    }
}

void AudioReactive::setBinding(std::size_t index, const AudioBinding& binding) {
    if (index < bindings_.size()) {
        bindings_[index] = binding;
    }
}

void AudioReactive::beginLink(AudioFeatureChannel channel) {
    pending_link_ = PendingLink{channel};
}

void AudioReactive::cancelLink() {
    pending_link_.reset();
}

bool AudioReactive::completeLink(OscParamTarget target, const std::string& node_path,
                                 std::uint32_t material_id) {
    if (!pending_link_.has_value()) {
        return false;
    }
    AudioBinding binding{};
    binding.channel = pending_link_->channel;
    binding.target = target;
    binding.node_path = node_path;
    binding.material_id = material_id;

    // Sensible defaults per target family.
    switch (target) {
    case OscParamTarget::FluidSourceDensity:
    case OscParamTarget::FluidSource2Density:
        binding.bias = 1.5f;
        binding.gain = 6.0f;
        binding.min_value = 0.0f;
        binding.max_value = 8.0f;
        break;
    case OscParamTarget::FluidSourceVelocityY:
        binding.bias = 0.15f;
        binding.gain = 0.9f;
        binding.min_value = -2.0f;
        binding.max_value = 2.0f;
        break;
    case OscParamTarget::FluidSource2VelocityY:
        binding.bias = -0.1f;
        binding.gain = -0.5f;
        binding.min_value = -2.0f;
        binding.max_value = 2.0f;
        break;
    case OscParamTarget::FluidSourceRadius:
        binding.bias = 0.08f;
        binding.gain = 0.12f;
        binding.min_value = 0.02f;
        binding.max_value = 0.35f;
        break;
    case OscParamTarget::LightIntensity:
        binding.bias = 0.5f;
        binding.gain = 4.0f;
        binding.min_value = 0.0f;
        binding.max_value = 20.0f;
        break;
    case OscParamTarget::ParticleRate:
        binding.bias = 8.0f;
        binding.gain = 40.0f;
        binding.min_value = 0.0f;
        binding.max_value = 120.0f;
        break;
    default:
        binding.bias = 0.0f;
        binding.gain = 1.0f;
        binding.min_value = 0.0f;
        binding.max_value = 1.0f;
        break;
    }

    addBinding(binding);
    cancelLink();
    return true;
}

void AudioReactive::addLavaLampBassPreset(const std::string& fluid_node_path) {
    AudioBinding density{};
    density.channel = AudioFeatureChannel::Bass;
    density.target = OscParamTarget::FluidSourceDensity;
    density.node_path = fluid_node_path;
    density.bias = 2.0f;
    density.gain = 6.5f;
    density.min_value = 0.5f;
    density.max_value = 8.0f;
    addBinding(density);

    AudioBinding rise{};
    rise.channel = AudioFeatureChannel::Beat;
    rise.target = OscParamTarget::FluidSourceVelocityY;
    rise.node_path = fluid_node_path;
    rise.bias = 0.2f;
    rise.gain = 0.85f;
    rise.min_value = 0.05f;
    rise.max_value = 1.4f;
    addBinding(rise);

    AudioBinding kick{};
    kick.channel = AudioFeatureChannel::Onset;
    kick.target = OscParamTarget::FluidSourceRadius;
    kick.node_path = fluid_node_path;
    kick.bias = 0.1f;
    kick.gain = 0.18f;
    kick.min_value = 0.06f;
    kick.max_value = 0.32f;
    addBinding(kick);
}

} // namespace genesis
