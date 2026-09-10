#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace genesis {

struct RvmInferParams {
    float downsample_ratio = 0.25f;
    std::uint32_t max_infer_side = 384; // hard cap on network input long side
    bool bilinear_upsample = true;      // false = nearest (much faster)
};

// Robust Video Matting (MobileNetV3) via ONNX Runtime.
// No NVIDIA Broadcast / Maxine SDK dependency.
class RvmOnnxSession {
public:
    RvmOnnxSession() = default;
    ~RvmOnnxSession();

    RvmOnnxSession(const RvmOnnxSession&) = delete;
    RvmOnnxSession& operator=(const RvmOnnxSession&) = delete;

    // prefer_cpu=true skips DirectML (required inside PIXERA: DML on the preview GPU
    // freezes Video In / shared output blit while the model loads / runs).
    // dml_device_id: DXGI adapter index for DML (-1 = auto). adapter_luid: prefer matching GPU.
    bool load(const std::filesystem::path& model_path, std::string* out_error = nullptr,
              bool prefer_cpu = false, int dml_device_id = -1, std::uint64_t adapter_luid = 0);
    bool ready() const { return ready_; }
    void reset();
    // "DML" / "CPU" / empty before load. After DML: may be "DML:0" etc.
    const std::string& executionProvider() const { return ep_name_; }

    // in_rgba: full-res RGBA8. Writes soft alpha [0,1] into out_alpha (width*height).
    bool inferAlpha(const std::uint8_t* in_rgba, std::uint32_t width, std::uint32_t height,
                    const RvmInferParams& params, std::vector<float>& out_alpha,
                    std::string* out_error = nullptr);

    std::uint32_t lastInferWidth() const { return last_infer_w_; }
    std::uint32_t lastInferHeight() const { return last_infer_h_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    bool ready_ = false;
    std::string ep_name_;
    std::uint32_t last_infer_w_ = 0;
    std::uint32_t last_infer_h_ = 0;
};

} // namespace genesis
