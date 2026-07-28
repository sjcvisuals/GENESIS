#include "matting/BgRemover.hpp"

#include <cstdio>
#include <vector>

namespace {

std::vector<std::uint8_t> makeGreenScreenPerson(std::uint32_t w, std::uint32_t h) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4u);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4u;
            // Green backdrop.
            rgba[i + 0] = 16;
            rgba[i + 1] = 220;
            rgba[i + 2] = 32;
            rgba[i + 3] = 255;
            // Soft "person" blob in the center (skin-ish).
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
    genesis::BgRemover remover;
    genesis::BgRemoverSettings settings{};
    settings.backend = genesis::BgRemoverBackend::ChromaKey;
    settings.key_r = 0.0f;
    settings.key_g = 1.0f;
    settings.key_b = 0.0f;
    settings.key_threshold = 0.28f;
    settings.key_softness = 0.1f;
    settings.temporal_alpha = 0.0f;
    remover.setSettings(settings);

    std::string status;
    if (!remover.initialize(&status)) {
        std::fprintf(stderr, "matting_smoke: init failed: %s\n", status.c_str());
        return 1;
    }

    constexpr std::uint32_t kW = 64;
    constexpr std::uint32_t kH = 64;
    const auto in = makeGreenScreenPerson(kW, kH);
    std::vector<std::uint8_t> out = in;
    if (!remover.process(in.data(), out.data(), kW, kH)) {
        std::fprintf(stderr, "matting_smoke: process failed\n");
        return 1;
    }

    // Center should stay opaque; corner green should go near-transparent.
    const std::size_t center = ((kH / 2u) * kW + (kW / 2u)) * 4u;
    const std::size_t corner = 3u;
    if (out[center] < 100 || out[center + 3] < 200) {
        std::fprintf(stderr, "matting_smoke: expected opaque person at center (a=%u)\n",
                     out[center + 3]);
        return 1;
    }
    if (out[corner] > 40) {
        std::fprintf(stderr, "matting_smoke: expected transparent green corner (a=%u)\n",
                     out[corner]);
        return 1;
    }

    // Auto backend should still succeed via chroma fallback without ORT/NVIDIA SDK.
    genesis::BgRemover auto_remover;
    genesis::BgRemoverSettings auto_settings{};
    auto_settings.backend = genesis::BgRemoverBackend::Auto;
    auto_remover.setSettings(auto_settings);
    std::string auto_status;
    auto_remover.initialize(&auto_status);
    if (auto_remover.activeBackend() != genesis::BgRemoverBackend::ChromaKey &&
        auto_remover.activeBackend() != genesis::BgRemoverBackend::RvmOnnx) {
        std::fprintf(stderr, "matting_smoke: unexpected auto backend\n");
        return 1;
    }

    std::printf("matting_smoke: ok (backend=%s status=%s center_a=%u corner_a=%u)\n",
                genesis::bgRemoverBackendLabel(remover.activeBackend()), status.c_str(),
                out[center + 3], out[corner]);
    std::printf("matting_smoke: auto → %s (%s)\n",
                genesis::bgRemoverBackendLabel(auto_remover.activeBackend()), auto_status.c_str());
    return 0;
}
