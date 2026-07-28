#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace genesis {

// Robust Video Matting (MobileNetV3) via ONNX Runtime.
// No NVIDIA Broadcast / Maxine SDK dependency.
class RvmOnnxSession {
public:
    RvmOnnxSession() = default;
    ~RvmOnnxSession();

    RvmOnnxSession(const RvmOnnxSession&) = delete;
    RvmOnnxSession& operator=(const RvmOnnxSession&) = delete;

    bool load(const std::filesystem::path& model_path, std::string* out_error = nullptr);
    bool ready() const { return ready_; }
    void reset();

    // in_rgba: full-res RGBA8. Writes soft alpha [0,1] into out_alpha (width*height).
    bool inferAlpha(const std::uint8_t* in_rgba, std::uint32_t width, std::uint32_t height,
                    float downsample_ratio, std::vector<float>& out_alpha, std::string* out_error = nullptr);

private:
    struct Impl;
    Impl* impl_ = nullptr;
    bool ready_ = false;
};

} // namespace genesis
