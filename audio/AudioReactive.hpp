#pragma once

#include "audio/AudioAnalyser.hpp"
#include "audio/AudioCapture.hpp"
#include "audio/AudioFeatures.hpp"
#include "protocol/OscTypes.hpp"
#include "scene/SceneRuntime.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace genesis {

struct AudioBinding {
    AudioFeatureChannel channel = AudioFeatureChannel::Bass;
    OscParamTarget target = OscParamTarget::FluidSourceDensity;
    std::string node_path;
    std::uint32_t material_id = 0;
    // mapped = clamp(bias + gain * feature, min_value, max_value)
    float gain = 1.0f;
    float bias = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
    bool enabled = true;
};

class AudioReactive {
public:
    struct Settings {
        bool enabled = true;
        float master_gain = 1.0f;
    };

    AudioCapture& capture() { return capture_; }
    const AudioCapture& capture() const { return capture_; }
    AudioAnalyser& analyser() { return analyser_; }
    const AudioAnalyser& analyser() const { return analyser_; }

    Settings& settings() { return settings_; }
    const Settings& settings() const { return settings_; }

    const AudioFeatures& features() const { return analyser_.features(); }
    const std::vector<AudioBinding>& bindings() const { return bindings_; }

    // Pump capture → analyse → apply bindings. Call once per frame from the editor/player.
    void tick(SceneRuntime& runtime, float dt_seconds);

    void removeBinding(std::size_t index);
    void clearBindings();
    void addBinding(const AudioBinding& binding);
    void setBinding(std::size_t index, const AudioBinding& binding);
    std::vector<AudioBinding>& bindingsMutable() { return bindings_; }

    // Inspector link flow (mirrors OSC).
    bool isAwaitingParameterLink() const { return pending_link_.has_value(); }
    AudioFeatureChannel pendingLinkChannel() const {
        return pending_link_.has_value() ? pending_link_->channel : AudioFeatureChannel::Bass;
    }
    void beginLink(AudioFeatureChannel channel);
    void cancelLink();
    bool completeLink(OscParamTarget target, const std::string& node_path,
                      std::uint32_t material_id = 0);

    // One-click: bass → fluid source density / velocity on a lava-lamp node.
    void addLavaLampBassPreset(const std::string& fluid_node_path);
    void addParticleBassRatePreset(const std::string& particle_node_path);

    const std::string& lastError() const { return last_error_; }
    void setLastError(std::string error) { last_error_ = std::move(error); }

private:
    Settings settings_{};
    AudioCapture capture_;
    AudioAnalyser analyser_;
    std::vector<AudioBinding> bindings_;

    struct PendingLink {
        AudioFeatureChannel channel = AudioFeatureChannel::Bass;
    };
    std::optional<PendingLink> pending_link_;
    std::string last_error_;
};

std::string audioBindingSummary(const AudioBinding& binding);

} // namespace genesis
