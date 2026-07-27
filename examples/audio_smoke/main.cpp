#include "audio/AudioAnalyser.hpp"
#include "audio/AudioReactive.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

std::vector<float> makeBassPulse(float sample_rate, float seconds, float hz) {
    const std::size_t n = static_cast<std::size_t>(sample_rate * seconds);
    std::vector<float> samples(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / sample_rate;
        const float env = std::exp(-t * 3.0f);
        samples[i] = env * 0.7f * std::sin(2.0f * 3.14159265358979323846f * hz * t);
    }
    return samples;
}

} // namespace

int main() {
    genesis::AudioAnalyser analyser;
    genesis::AudioAnalyser::Settings settings{};
    settings.sample_rate = 44100.0f;
    analyser.setSettings(settings);

    const auto pulse = makeBassPulse(44100.0f, 0.35f, 60.0f);
    analyser.pushMono(pulse.data(), pulse.size());

    bool saw_bass = false;
    for (int i = 0; i < 30; ++i) {
        analyser.tick(1.0f / 60.0f);
        if (analyser.features().bass > 0.05f) {
            saw_bass = true;
        }
    }

    if (!saw_bass) {
        std::fprintf(stderr, "audio_smoke: expected bass energy from 60 Hz pulse\n");
        return 1;
    }

    // Binding path: bass → fluid source density on a temp scene.
    // SceneRuntime needs a Vulkan context — skip full runtime; test applySceneParam needs
    // a live SceneRuntime. Instead verify mapping math via AudioReactive helpers.
    genesis::AudioReactive reactive;
    reactive.addLavaLampBassPreset("/Lava Lamp");
    if (reactive.bindings().size() < 3) {
        std::fprintf(stderr, "audio_smoke: lava preset should add 3 bindings\n");
        return 1;
    }

    std::printf("audio_smoke: ok (bass=%.3f onset=%.3f beat=%.3f bindings=%zu)\n",
                analyser.features().bass, analyser.features().onset, analyser.features().beat,
                reactive.bindings().size());
    return 0;
}
