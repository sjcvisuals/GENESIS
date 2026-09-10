#pragma once

#include "matting/VisionEffect.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace genesis {

struct MapOnnxConfig {
    VisionNorm norm = VisionNorm::ImageNet;
    VisionActivation activation = VisionActivation::Auto;
    int size_override = 0;            // 0 = use model fixed size (or max_side)
    std::uint32_t max_side = 1024;    // cap when size is dynamic / override unset
};

// Generic single-channel-map (or 3-channel colour) ONNX session.
// RGBA8 in → float map HxW or RGBA8 colour out. Layout/size auto-detected from the model.
class MapOnnxSession {
public:
    MapOnnxSession() = default;
    ~MapOnnxSession();

    MapOnnxSession(const MapOnnxSession&) = delete;
    MapOnnxSession& operator=(const MapOnnxSession&) = delete;

    // prefer_cpu=true skips DirectML (same rationale as RvmOnnxSession).
    // dml_device_id: DXGI adapter index for DML (-1 = auto). adapter_luid: reserved for caller.
    bool load(const std::filesystem::path& model_path, std::string* out_error = nullptr,
              bool prefer_cpu = false, int dml_device_id = -1, std::uint64_t adapter_luid = 0);
    bool ready() const { return ready_; }
    void reset();
    // "DML" / "CPU" / empty before load. After DML: may be "DML:0" etc.
    const std::string& executionProvider() const { return ep_name_; }
    bool isColourOutput() const { return colour_out_; }
    double lastInferenceMs() const { return last_infer_ms_; }
    std::uint32_t lastInferWidth() const { return last_infer_w_; }
    std::uint32_t lastInferHeight() const { return last_infer_h_; }

    // Single-channel map at full resolution (width*height floats, typically [0,1]).
    bool inferMap(const std::uint8_t* in_rgba, std::uint32_t width, std::uint32_t height,
                  const MapOnnxConfig& config, std::vector<float>& out_map,
                  std::string* out_error = nullptr);

    // 3-channel image out as full-res RGBA8 (alpha = 255).
    bool inferColour(const std::uint8_t* in_rgba, std::uint32_t width, std::uint32_t height,
                     const MapOnnxConfig& config, std::vector<std::uint8_t>& out_rgba,
                     std::string* out_error = nullptr);

private:
    struct Impl;
    Impl* impl_ = nullptr;
    bool ready_ = false;
    bool colour_out_ = false;
    bool nchw_ = true;
    bool input_fp16_ = false;
    bool output_fp16_ = false;
    std::string ep_name_;
    std::string input_name_;
    std::string output_name_;
    std::int64_t model_h_ = 0; // 0 = dynamic
    std::int64_t model_w_ = 0;
    std::int64_t model_c_in_ = 3;
    std::int64_t model_c_out_ = 1;
    std::uint32_t last_infer_w_ = 0;
    std::uint32_t last_infer_h_ = 0;
    double last_infer_ms_ = 0.0;
};

} // namespace genesis
