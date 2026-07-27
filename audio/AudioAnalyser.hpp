#pragma once

#include "audio/AudioFeatures.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace genesis {

// Cheap mono FFT analyser: Hann window → radix-2 FFT → band energies → envelopes.
// Designed for ~30–60 analyses/sec (once per frame), FFT size 512.
class AudioAnalyser {
public:
    static constexpr int kFftSize = 512;
    static constexpr int kRingCapacity = 4096;

    struct Settings {
        float input_gain = 1.0f;
        float bass_gain = 1.35f;
        float mid_gain = 1.0f;
        float treble_gain = 1.15f;
        float attack_seconds = 0.02f;
        float release_seconds = 0.18f;
        float beat_release_seconds = 0.45f;
        float onset_sensitivity = 1.6f;
        float sample_rate = 44100.0f;
    };

    void setSettings(const Settings& settings) { settings_ = settings; }
    const Settings& settings() const { return settings_; }

    // Push interleaved or mono float samples (main thread or after downmix).
    void pushMono(const float* samples, std::size_t count);
    void clear();

    // Analyse latest window if enough samples; updates features().
    // Call once per frame. Returns true when a new FFT was computed.
    bool tick(float dt_seconds);

    const AudioFeatures& features() const { return features_; }

private:
    void ensureBuffers();
    void smooth(float& state, float target, float dt, float attack, float release) const;

    Settings settings_{};
    AudioFeatures features_{};

    std::vector<float> ring_;
    std::size_t ring_write_ = 0;
    std::size_t ring_filled_ = 0;

    std::vector<float> window_buf_;
    std::vector<float> fft_re_;
    std::vector<float> fft_im_;
    std::vector<float> hann_;
    std::vector<float> prev_mag_;

    float prev_bass_raw_ = 0.0f;
};

} // namespace genesis
