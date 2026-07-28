#include "matting/BgRemover.hpp"

#include "matting/RvmOnnxSession.hpp"
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
    return resolveAssetPath("assets/matting/rvm_mobilenetv3_fp16.onnx");
}

float downsampleForQuality(BgRemoverQuality q, float override_ratio) {
    if (override_ratio > 0.0f) {
        return override_ratio;
    }
    switch (q) {
    case BgRemoverQuality::Fast:
        return 0.2f;
    case BgRemoverQuality::Quality:
        return 0.35f;
    case BgRemoverQuality::Balanced:
    default:
        return 0.25f;
    }
}

} // namespace

struct BgRemover::RvmState {
    RvmOnnxSession session;
    std::vector<float> alpha;
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
    if (rvm_) {
        rvm_->session.reset();
    }
}

bool BgRemover::tryInitRvm(std::string* out_status) {
    if (!rvm_) {
        rvm_ = new RvmState();
    }
    std::string error;
    if (!rvm_->session.load(settings_.model_path, &error)) {
        if (out_status) {
            *out_status = error;
        }
        return false;
    }
    if (out_status) {
        *out_status = "rvm-onnx loaded: " + settings_.model_path.string();
    }
    return true;
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
        status_ = rvm_status;
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
    if (!rvm_ || !rvm_->session.ready()) {
        return false;
    }
    const float ds = downsampleForQuality(settings_.quality, settings_.rvm_downsample);
    std::string error;
    if (!rvm_->session.inferAlpha(in_rgba, width, height, ds, rvm_->alpha, &error)) {
        status_ = error;
        return false;
    }

    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    if (rvm_->alpha.size() != pixels) {
        return false;
    }
    if (prev_alpha_.size() != pixels) {
        prev_alpha_.assign(pixels, 1.0f);
    }
    const float temporal = clampf(settings_.temporal_alpha, 0.0f, 0.95f);

    for (std::size_t i = 0; i < pixels; ++i) {
        float alpha = clampf(rvm_->alpha[i], 0.0f, 1.0f);
        alpha = alpha * (1.0f - temporal) + prev_alpha_[i] * temporal;
        prev_alpha_[i] = alpha;
        const float r = in_rgba[i * 4 + 0] / 255.0f;
        const float g = in_rgba[i * 4 + 1] / 255.0f;
        const float b = in_rgba[i * 4 + 2] / 255.0f;
        out_rgba[i * 4 + 0] = static_cast<std::uint8_t>(std::lround(r * alpha * 255.0f));
        out_rgba[i * 4 + 1] = static_cast<std::uint8_t>(std::lround(g * alpha * 255.0f));
        out_rgba[i * 4 + 2] = static_cast<std::uint8_t>(std::lround(b * alpha * 255.0f));
        out_rgba[i * 4 + 3] = static_cast<std::uint8_t>(std::lround(alpha * 255.0f));
    }
    return true;
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
