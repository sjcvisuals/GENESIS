// Headless matte gate (no PIXERA, no camera).
// Build: genesis_matting_smoke
// Run:   genesis_matting_smoke --require-rvm

#include "matting/AsyncMatteWorker.hpp"
#include "matting/BgRemover.hpp"
#include "matting/VisionEffect.hpp"

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
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
            if (cx * cx * 2.2f + cy * cy * 1.4f < 0.045f) {
                rgba[i + 0] = 210;
                rgba[i + 1] = 160;
                rgba[i + 2] = 130;
            }
        }
    }
    return rgba;
}

} // namespace

int main(int argc, char** argv) {
    bool require_rvm = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--require-rvm" || arg == "--require-rvm=1") {
            require_rvm = true;
        } else if (arg == "--help" || arg == "-h") {
            std::printf(
                "genesis_matting_smoke [--require-rvm]\n"
                "  Headless chroma + RVM + async matte checks (no PIXERA, no camera).\n"
                "  --require-rvm  Fail if RVM/ORT did not initialize (demo gate).\n");
            return 0;
        }
    }
    if (const char* env = std::getenv("GENESIS_REQUIRE_RVM")) {
        if (env[0] == '1' || env[0] == 'y' || env[0] == 'Y') {
            require_rvm = true;
        }
    }

    // --- Chroma path ---
    {
        genesis::BgRemover remover;
        genesis::BgRemoverSettings settings{};
        settings.backend = genesis::BgRemoverBackend::ChromaKey;
        settings.key_threshold = 0.28f;
        settings.key_softness = 0.10f;
        settings.temporal_alpha = 0.0f;
        remover.setSettings(settings);
        std::string status;
        if (!remover.initialize(&status)) {
            std::fprintf(stderr, "matting_smoke: chroma initialize failed (%s)\n", status.c_str());
            return 1;
        }

        constexpr std::uint32_t kW = 64;
        constexpr std::uint32_t kH = 64;
        const auto in = makeGreenScreenPerson(kW, kH);
        std::vector<std::uint8_t> out = in;
        if (!remover.process(in.data(), out.data(), kW, kH)) {
            std::fprintf(stderr, "matting_smoke: chroma process failed\n");
            return 1;
        }
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
    auto_settings.prefer_cpu_ep = true;
    auto_settings.rvm_downsample = 0.5f;
    auto_settings.temporal_alpha = 0.0f;
    auto_remover.setSettings(auto_settings);
    std::string auto_status;
    auto_remover.initialize(&auto_status);
    std::printf("matting_smoke: auto -> %s (%s)\n",
                genesis::bgRemoverBackendLabel(auto_remover.activeBackend()), auto_status.c_str());

    if (auto_remover.isRvmReady()) {
        constexpr std::uint32_t kW = 160;
        constexpr std::uint32_t kH = 160;
        const auto in = makeGreenScreenPerson(kW, kH);
        std::vector<std::uint8_t> out = in;
        if (!auto_remover.process(in.data(), out.data(), kW, kH) ||
            !auto_remover.process(in.data(), out.data(), kW, kH)) {
            std::fprintf(stderr, "matting_smoke: RVM process failed\n");
            return 1;
        }
        const std::size_t center = ((kH / 2u) * kW + (kW / 2u)) * 4u;
        if (out[center + 3] == 0 && out[3] == 255) {
            std::fprintf(stderr, "matting_smoke: RVM produced inverted/empty matte\n");
            return 1;
        }
        std::printf("matting_smoke: rvm ok ep=%s center_a=%u corner_a=%u\n",
                    auto_remover.executionProvider().c_str(), out[center + 3], out[3]);
    } else if (require_rvm) {
        std::fprintf(stderr,
                     "matting_smoke: RVM required but not ready (%s). "
                     "Check onnxruntime + assets/matting/rvm_mobilenetv3_fp16.onnx\n",
                     auto_status.c_str());
        return 2;
    } else {
        std::printf("matting_smoke: rvm skipped (fallback active)\n");
    }

    // --- Async worker ---
    {
        genesis::AsyncMatteWorker worker;
        genesis::BgRemoverSettings settings{};
        settings.backend = genesis::BgRemoverBackend::Auto;
        settings.quality = genesis::BgRemoverQuality::Quality;
        settings.prefer_cpu_ep = true;
        settings.rvm_downsample = 0.35f;
        settings.temporal_alpha = 0.0f;
        worker.setSettings(settings);
        worker.start();

        constexpr std::uint32_t kW = 320;
        constexpr std::uint32_t kH = 320;
        const auto in = makeGreenScreenPerson(kW, kH);
        std::vector<std::uint8_t> out(static_cast<std::size_t>(kW) * kH * 4u, 0);

        worker.trySubmit(in.data(), kW, kH);
        for (int i = 0; i < 64; ++i) {
            worker.trySubmit(in.data(), kW, kH);
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (worker.completed() == 0 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (worker.completed() == 0) {
            std::fprintf(stderr, "matting_smoke: async worker produced no frames\n");
            return 1;
        }
        if (!worker.copyLatest(out.data(), kW, kH)) {
            std::fprintf(stderr, "matting_smoke: async copyLatest failed\n");
            return 1;
        }
        if (worker.skipped() == 0) {
            std::fprintf(stderr,
                         "matting_smoke: expected skips while busy (submitted=%llu completed=%llu)\n",
                         static_cast<unsigned long long>(worker.submitted()),
                         static_cast<unsigned long long>(worker.completed()));
            return 1;
        }
        std::printf("matting_smoke: async ok submitted=%llu completed=%llu skipped=%llu center_a=%u\n",
                    static_cast<unsigned long long>(worker.submitted()),
                    static_cast<unsigned long long>(worker.completed()),
                    static_cast<unsigned long long>(worker.skipped()),
                    out[((kH / 2u) * kW + (kW / 2u)) * 4u + 3u]);
        worker.stop();
    }

    // --- Soft-fail missing vision model ---
    {
        genesis::BgRemover vision;
        genesis::BgRemoverSettings vs{};
        vs.vision_effect = genesis::VisionEffect::DepthMap;
        vs.result_as = genesis::VisionResultAs::Greyscale;
        vs.temporal_alpha = 0.0f;
        vs.model_path = "assets/vision/__missing_depth__.onnx";
        vision.setSettings(vs);
        std::string vst;
        vision.initialize(&vst);
        std::printf("matting_smoke: depth missing-model -> %s (%s)\n",
                    genesis::bgRemoverBackendLabel(vision.activeBackend()), vst.c_str());
        constexpr std::uint32_t kW = 32;
        constexpr std::uint32_t kH = 32;
        const auto in = makeGreenScreenPerson(kW, kH);
        std::vector<std::uint8_t> out = in;
        if (!vision.process(in.data(), out.data(), kW, kH)) {
            std::fprintf(stderr, "matting_smoke: vision soft-fail process failed\n");
            return 1;
        }
        if (out[3] != 255) {
            std::fprintf(stderr, "matting_smoke: expected opaque passthrough alpha\n");
            return 1;
        }
        std::printf("matting_smoke: vision soft-fail passthrough ok\n");
    }

    // --- Empty model_path must keep a warm RVM session ---
    if (auto_remover.isRvmReady()) {
        genesis::BgRemoverSettings keep = auto_remover.settings();
        keep.model_path.clear();
        keep.result_as = genesis::VisionResultAs::Greyscale;
        auto_remover.setSettings(keep);
        if (!auto_remover.isRvmReady() ||
            auto_remover.activeBackend() != genesis::BgRemoverBackend::RvmOnnx) {
            std::fprintf(stderr, "matting_smoke: empty model_path cleared RVM session\n");
            return 1;
        }
        std::printf("matting_smoke: empty model_path keeps RVM session ok\n");

        const std::string ep_before = auto_remover.executionProvider();
        std::string rein;
        if (!auto_remover.initialize(&rein) || !auto_remover.isRvmReady()) {
            std::fprintf(stderr, "matting_smoke: idempotent initialize lost RVM\n");
            return 1;
        }
        if (auto_remover.executionProvider() != ep_before) {
            std::fprintf(stderr, "matting_smoke: idempotent initialize changed EP\n");
            return 1;
        }
        std::printf("matting_smoke: idempotent initialize ok (%s)\n", ep_before.c_str());
    }

    std::printf("matting_smoke: ALL PASS\n");
    return 0;
}
