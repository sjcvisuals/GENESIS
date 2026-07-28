#include "matting/BgRemover.hpp"

#include "platform/Paths.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace genesis {

namespace {

float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

std::filesystem::path defaultModelPath() {
    return resolveAssetPath("matting/rvm_mobilenetv3_fp16.onnx");
}

} // namespace

struct BgRemover::RvmState {
    // Reserved for ONNX Runtime session + recurrent tensors.
    // Populated when GENESIS_HAS_ONNXRUNTIME is enabled and a model loads.
    bool placeholder = true;
};

const char* bgRemoverBackendLabel(BgRemoverBackend backend) {
    switch (backend) {
    case BgRemoverBackend::Auto:
        return "Auto";
    case BgRemoverBackend::RvmOnnx:
        return "RVM (ONNX)";
    case BgRemoverBackend::ChromaKey:
        return "Chroma Key";
    case BgRemoverBackend::Passthrough:
        return "Passthrough";
    default:
        return "Unknown";
    }
}

const char* bgRemoverQualityLabel(BgRemoverQuality quality) {
    switch (quality) {
    case BgRemoverQuality::Fast:
        return "Fast";
    case BgRemoverQuality::Balanced:
        return "Balanced";
    case BgRemoverQuality::Quality:
        return "Quality";
    default:
        return "Unknown";
    }
}

BgRemover::BgRemover() = default;

BgRemover::~BgRemover() {
    delete rvm_;
    rvm_ = nullptr;
}

void BgRemover::setSettings(const BgRemoverSettings& settings) {
    settings_ = settings;
    initialized_ = false;
    rvm_ready_ = false;
}

void BgRemover::resetTemporalState() {
    prev_alpha_.clear();
}

bool BgRemover::tryInitRvm(std::string* out_status) {
#if defined(GENESIS_HAS_ONNXRUNTIME)
    // Future: Ort::Env + InferenceSession on settings_.model_path (or default).
    // Bundled model: assets/matting/rvm_mobilenetv3_fp16.onnx (see scripts/fetch-rvm-model.sh).
    (void)out_status;
    return false;
#else
    if (out_status) {
        *out_status =
            "RVM backend requires ONNX Runtime (build with GENESIS_HAS_ONNXRUNTIME). "
            "Using chroma-key fallback — no NVIDIA Broadcast SDK needed either way.";
    }
    return false;
#endif
}

bool BgRemover::initialize(std::string* out_status) {
    if (settings_.model_path.empty()) {
        try {
            settings_.model_path = defaultModelPath();
        } catch (const std::exception&) {
            settings_.model_path = "assets/matting/rvm_mobilenetv3_fp16.onnx";
        }
    }

    rvm_ready_ = false;
    if (settings_.backend == BgRemoverBackend::Passthrough) {
        active_backend_ = BgRemoverBackend::Passthrough;
        status_ = "passthrough";
        initialized_ = true;
        if (out_status) *out_status = status_;
        return true;
    }
    if (settings_.backend == BgRemoverBackend::ChromaKey) {
        active_backend_ = BgRemoverBackend::ChromaKey;
        status_ = "chroma-key";
        initialized_ = true;
        if (out_status) *out_status = status_;
        return true;
    }

    std::string rvm_status;
    if ((settings_.backend == BgRemoverBackend::Auto || settings_.backend == BgRemoverBackend::RvmOnnx) &&
        tryInitRvm(&rvm_status)) {
        active_backend_ = BgRemoverBackend::RvmOnnx;
        rvm_ready_ = true;
        status_ = "rvm-onnx ready";
        initialized_ = true;
        if (out_status) *out_status = status_;
        return true;
    }

    if (settings_.backend == BgRemoverBackend::RvmOnnx) {
        status_ = rvm_status.empty() ? "rvm unavailable" : rvm_status;
        active_backend_ = BgRemoverBackend::ChromaKey;
        initialized_ = true;
        if (out_status) *out_status = status_;
        return false;
    }

    // Auto → chroma fallback (still realtime, no extra SDK install).
    active_backend_ = BgRemoverBackend::ChromaKey;
    status_ = rvm_status.empty() ? "auto: chroma-key (RVM not loaded)" : ("auto: " + rvm_status);
    initialized_ = true;
    if (out_status) *out_status = status_;
    return true;
}

void BgRemover::processPassthrough(const std::uint8_t* in_rgba, std::uint8_t* out_rgba,
                                   std::uint32_t width, std::uint32_t height) {
    const std::size_t n = static_cast<std::size_t>(width) * height * 4u;
    if (out_rgba != in_rgba) {
        std::memcpy(out_rgba, in_rgba, n);
    }
    for (std::size_t i = 3; i < n; i += 4) {
        out_rgba[i] = 255;
    }
}

void BgRemover::processChroma(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::uint32_t width,
                              std::uint32_t height) {
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    if (prev_alpha_.size() != pixels) {
        prev_alpha_.assign(pixels, 1.0f);
    }

    const float thr = std::max(settings_.key_threshold, 1.0e-4f);
    const float soft = std::max(settings_.key_softness, 1.0e-4f);
    const float temporal = clampf(settings_.temporal_alpha, 0.0f, 0.95f);

    for (std::size_t i = 0; i < pixels; ++i) {
        const std::uint8_t* p = in_rgba + i * 4;
        const float r = p[0] / 255.0f;
        const float g = p[1] / 255.0f;
        const float b = p[2] / 255.0f;
        const float dr = r - settings_.key_r;
        const float dg = g - settings_.key_g;
        const float db = b - settings_.key_b;
        const float dist = std::sqrt(dr * dr + dg * dg + db * db);
        float alpha = clampf((dist - thr) / soft, 0.0f, 1.0f);
        alpha = alpha * (1.0f - temporal) + prev_alpha_[i] * temporal;
        prev_alpha_[i] = alpha;

        out_rgba[i * 4 + 0] = static_cast<std::uint8_t>(std::lround(r * alpha * 255.0f));
        out_rgba[i * 4 + 1] = static_cast<std::uint8_t>(std::lround(g * alpha * 255.0f));
        out_rgba[i * 4 + 2] = static_cast<std::uint8_t>(std::lround(b * alpha * 255.0f));
        out_rgba[i * 4 + 3] = static_cast<std::uint8_t>(std::lround(alpha * 255.0f));
    }
}

bool BgRemover::processRvm(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::uint32_t width,
                           std::uint32_t height) {
    (void)in_rgba;
    (void)out_rgba;
    (void)width;
    (void)height;
    return false;
}

bool BgRemover::process(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::uint32_t width,
                        std::uint32_t height) {
    if (!in_rgba || !out_rgba || width == 0 || height == 0) {
        return false;
    }
    if (!initialized_) {
        initialize(nullptr);
    }

    switch (active_backend_) {
    case BgRemoverBackend::Passthrough:
        processPassthrough(in_rgba, out_rgba, width, height);
        return true;
    case BgRemoverBackend::RvmOnnx:
        if (processRvm(in_rgba, out_rgba, width, height)) {
            return true;
        }
        processChroma(in_rgba, out_rgba, width, height);
        return true;
    case BgRemoverBackend::ChromaKey:
    case BgRemoverBackend::Auto:
    default:
        processChroma(in_rgba, out_rgba, width, height);
        return true;
    }
}

} // namespace genesis
