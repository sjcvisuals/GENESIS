#pragma once

#include <array>
#include <cstdint>

namespace genesis {

// Lightweight realtime features for driving visuals. All values are 0–1ish
// after envelope smoothing (onset spikes briefly).
struct AudioFeatures {
    float rms = 0.0f;
    float bass = 0.0f;    // ~20–150 Hz energy
    float mid = 0.0f;     // ~150–2000 Hz
    float treble = 0.0f;  // ~2000+ Hz
    float onset = 0.0f;   // spectral-flux transient (kick/snare hits)
    float beat = 0.0f;    // slower bass envelope useful for "drop" pulses

    // Compact UI spectrum (log-ish bands). Cheap to fill from the same FFT.
    static constexpr int kSpectrumBars = 16;
    std::array<float, kSpectrumBars> spectrum{};
};

enum class AudioFeatureChannel : std::uint8_t {
    Bass = 0,
    Mid,
    Treble,
    Rms,
    Onset,
    Beat,
};

const char* audioFeatureChannelLabel(AudioFeatureChannel channel);
float audioFeatureValue(const AudioFeatures& features, AudioFeatureChannel channel);

} // namespace genesis
