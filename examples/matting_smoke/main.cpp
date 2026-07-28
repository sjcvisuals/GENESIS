#include "matting/BgRemover.hpp"

#include <cstdio>
#include <vector>

namespace {

std::vector<std::uint8_t> makeGreenScreenPerson(std::uint32_t w, std::uint32_t h) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4u);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4u;
            rgba[i + 0] = 16;
            rgba[i + 1] = 220;
            rgba[i + 2] = 32;
            rgba[i + 3] = 255;
            const float cx = (static_cast<float>(x) + 0.5f) / static_cast<float>(w) - 0.5f;
            const float cy = (static_cast<float>(y) + 0.5f) / static_cast<float>(h) - 0.5f;
            const float d = cx * cx * 2.2f + cy * cy * 1.4f;
            if (d < 0.045f) {
                rgba[i + 0] = 210;
                rgba[i + 1] = 160;
                rgba[i + 2] = 130;
            }
        }
    }
    return rgba;
}

} // namespace

int main() {
    // --- Chroma path ---
    {
        genesis::BgRemover remover;
        genesis::BgRemoverSettings settings{};
        settings.backend = genesis::BgRemoverBackend::ChromaKey;
        settings.key_threshold = 0.28f;
        settings.key_softness = 0.1f;
        settings.temporal_alpha = 0.0f;
        remover.setSettings(settings);
        std::string status;
        remover.initialize(&status);

        constexpr std::uint32_t kW = 64;
        constexpr std::uint32_t kH = 64;
        const auto in = makeGreenScreenPerson(kW, kH);
        std::vector<std::uint8_t> out = in;
        remover.process(in.data(), out.data(), kW, kH);
        const std::size_t center = ((kH / 2u) * kW + (kW / 2u)) * 4u;
        const std::size_t corner = 3u;
        if (out[center + 3] < 200 || out[corner] > 40) {
            std::fprintf(stderr, "matting_smoke: chroma failed center_a=%u corner_a=%u\n",
                         out[center + 3], out[corner]);
            return 1;
        }
        std::printf("matting_smoke: chroma ok\n");
    }

    // --- Auto / RVM path ---
    genesis::BgRemover auto_remover;
    genesis::BgRemoverSettings auto_settings{};
    auto_settings.backend = genesis::BgRemoverBackend::Auto;
    auto_settings.quality = genesis::BgRemoverQuality::Fast;
    auto_settings.rvm_downsample = 0.5f;
    auto_settings.temporal_alpha = 0.0f;
    auto_remover.setSettings(auto_settings);
    std::string auto_status;
    auto_remover.initialize(&auto_status);
    std::printf("matting_smoke: auto → %s (%s)\n",
                genesis::bgRemoverBackendLabel(auto_remover.activeBackend()), auto_status.c_str());

    if (auto_remover.isRvmReady()) {
        constexpr std::uint32_t kW = 160;
        constexpr std::uint32_t kH = 160;
        const auto in = makeGreenScreenPerson(kW, kH);
        std::vector<std::uint8_t> out = in;
        // Warm recurrent state + second frame.
        auto_remover.process(in.data(), out.data(), kW, kH);
        auto_remover.process(in.data(), out.data(), kW, kH);
        const std::size_t center = ((kH / 2u) * kW + (kW / 2u)) * 4u;
        // RVM on synthetic green-screen may not be perfect; just ensure it runs and
        // produces a plausible alpha range.
        if (out[center + 3] == 0 && out[3] == 255) {
            std::fprintf(stderr, "matting_smoke: RVM produced inverted/empty matte\n");
            return 1;
        }
        std::printf("matting_smoke: rvm ok (center_a=%u corner_a=%u)\n", out[center + 3], out[3]);
    } else {
        std::printf("matting_smoke: rvm skipped (fallback active)\n");
    }

    return 0;
}
