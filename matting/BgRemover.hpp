#pragma once

#include "matting/VisionEffect.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace genesis {

enum class BgRemoverBackend : std::uint8_t {
    Auto = 0,       // RVM if model+ORT available, else chroma
    RvmOnnx,        // Robust Video Matting via ONNX Runtime (no NVIDIA Broadcast SDK)
    ChromaKey,      // Classical green/blue key — cheapest realtime path
    Passthrough,    // Copy RGB, force opaque (debug)
    VisionMap,      // Generic map ONNX (depth / BiRefNet / saliency / parse)
    VisionColour,   // Colour / style ONNX
};

enum class BgRemoverQuality : std::uint8_t {
    Fast = 0,     // Infer ~256–384, stronger downsample
    Balanced = 1, // Infer ~512 (default)
    Quality = 2,  // Infer ~640–720
};

struct BgRemoverSettings {
    BgRemoverBackend backend = BgRemoverBackend::Auto;
    BgRemoverQuality quality = BgRemoverQuality::Balanced;
    // Which vision effect to run. PersonMatteRvm keeps the RVM path; others use MapOnnxSession.
    VisionEffect vision_effect = VisionEffect::PersonMatteRvm;
    VisionResultAs result_as = VisionResultAs::Alpha;
    // Optional overrides (Auto / 0 = use effect defaults).
    VisionActivation activation = VisionActivation::Auto;
    VisionNorm norm = VisionNorm::ImageNet;
    int size_override = 0;
    // Chroma key (also used as Auto fallback).
    float key_r = 0.0f;
    float key_g = 1.0f;
    float key_b = 0.0f;
    float key_threshold = 0.35f;
    float key_softness = 0.12f;
    // Temporal blend on alpha [0=off, 1=sticky].
    float temporal_alpha = 0.35f;
    // RVM downsample override. 0 = use Quality preset (Fast/Balanced/Quality).
    float rvm_downsample = 0.0f;
    // Hard cap on network input long side. 0 = use quality preset.
    std::uint32_t rvm_max_infer_side = 0;
    // Empty → effect default under assets/matting or assets/vision.
    std::filesystem::path model_path;
    // Skip DirectML — use ORT CPU EP. Required for PIXERA Remover worker (DML freezes GPU).
    bool prefer_cpu_ep = false;
    // DirectML adapter index (DXGI EnumAdapters). -1 = try LUID match then 0,1,2...
    int dml_device_id = -1;
    // When non-zero, prefer the DXGI adapter whose LUID matches (PIXERA preview GPU).
    std::uint64_t adapter_luid = 0;
};

// CPU RGBA8 in → premultiplied RGBA8 out (or greyscale/colour visualisation).
class BgRemover {
public:
    BgRemover();
    ~BgRemover();

    BgRemover(const BgRemover&) = delete;
    BgRemover& operator=(const BgRemover&) = delete;

    void setSettings(const BgRemoverSettings& settings);
    const BgRemoverSettings& settings() const { return settings_; }

    bool initialize(std::string* out_status = nullptr);

    // True after a successful initialize() until setSettings() invalidates the session.
    bool isInitialized() const { return initialized_; }
    bool isRvmReady() const { return rvm_ready_; }
    bool isVisionReady() const { return vision_ready_; }
    BgRemoverBackend activeBackend() const { return active_backend_; }
    const std::string& status() const { return status_; }
    std::uint32_t lastInferWidth() const;
    std::uint32_t lastInferHeight() const;
    std::string executionProvider() const;

    bool process(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::uint32_t width,
                 std::uint32_t height);

    void resetTemporalState();

private:
    bool tryInitRvm(std::string* out_status);
    bool tryInitVision(std::string* out_status);
    void processChroma(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::uint32_t width,
                       std::uint32_t height);
    void processPassthrough(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::uint32_t width,
                            std::uint32_t height);
    bool processRvm(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::uint32_t width,
                    std::uint32_t height);
    bool processVision(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::uint32_t width,
                       std::uint32_t height);
    void applyMapResult(const std::uint8_t* in_rgba, std::uint8_t* out_rgba,
                        const std::vector<float>& map, std::uint32_t width, std::uint32_t height);
    std::filesystem::path resolveModelPath() const;
    bool usesVisionSession() const;

    BgRemoverSettings settings_{};
    BgRemoverBackend active_backend_ = BgRemoverBackend::ChromaKey;
    bool rvm_ready_ = false;
    bool vision_ready_ = false;
    bool initialized_ = false;
    std::string status_ = "uninitialized";

    std::vector<float> prev_alpha_;
    std::vector<float> vision_map_;
    struct RvmState;
    RvmState* rvm_ = nullptr;
    struct VisionState;
    VisionState* vision_ = nullptr;
};

const char* bgRemoverBackendLabel(BgRemoverBackend backend);
const char* bgRemoverQualityLabel(BgRemoverQuality quality);

} // namespace genesis
