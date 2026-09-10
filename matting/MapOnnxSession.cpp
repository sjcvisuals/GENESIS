#include "matting/MapOnnxSession.hpp"

#if defined(GENESIS_HAS_ONNXRUNTIME)

#include <onnxruntime_cxx_api.h>
#include <onnxruntime_float16.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace genesis {

namespace {

Ort::Float16_t toF16(float v) { return Ort::Float16_t(v); }
float fromF16(Ort::Float16_t v) { return static_cast<float>(v); }

void normalizePixel(float r, float g, float b, VisionNorm norm, float& orr, float& og, float& ob) {
    switch (norm) {
    case VisionNorm::ImageNet:
        orr = (r - 0.485f) / 0.229f;
        og = (g - 0.456f) / 0.224f;
        ob = (b - 0.406f) / 0.225f;
        break;
    case VisionNorm::MinusOneToOne:
        orr = r * 2.0f - 1.0f;
        og = g * 2.0f - 1.0f;
        ob = b * 2.0f - 1.0f;
        break;
    case VisionNorm::ZeroToOne:
    default:
        orr = r;
        og = g;
        ob = b;
        break;
    }
}

// Bilinear resize RGBA8 → float RGB (0..1), then normalize into NCHW or NHWC float buffer.
void preprocessBilinear(const std::uint8_t* src_rgba, std::uint32_t sw, std::uint32_t sh,
                        std::vector<float>& dst, std::uint32_t dw, std::uint32_t dh, bool nchw,
                        VisionNorm norm) {
    const std::size_t plane = static_cast<std::size_t>(dw) * dh;
    dst.resize(plane * 3u);
    float* out = dst.data();

    for (std::uint32_t y = 0; y < dh; ++y) {
        const float fy =
            (static_cast<float>(y) + 0.5f) * static_cast<float>(sh) / static_cast<float>(dh) - 0.5f;
        const int y0 = std::max(0, static_cast<int>(std::floor(fy)));
        const int y1 = std::min(static_cast<int>(sh) - 1, y0 + 1);
        const float ty = fy - static_cast<float>(y0);
        for (std::uint32_t x = 0; x < dw; ++x) {
            const float fx =
                (static_cast<float>(x) + 0.5f) * static_cast<float>(sw) / static_cast<float>(dw) -
                0.5f;
            const int x0 = std::max(0, static_cast<int>(std::floor(fx)));
            const int x1 = std::min(static_cast<int>(sw) - 1, x0 + 1);
            const float tx = fx - static_cast<float>(x0);

            auto sample = [&](int sx, int sy, int c) -> float {
                return static_cast<float>(
                           src_rgba[(static_cast<std::size_t>(sy) * sw + static_cast<std::size_t>(sx)) *
                                        4u +
                                    static_cast<std::size_t>(c)]) /
                       255.0f;
            };

            float rgb[3];
            for (int c = 0; c < 3; ++c) {
                const float a00 = sample(x0, y0, c);
                const float a01 = sample(x1, y0, c);
                const float a10 = sample(x0, y1, c);
                const float a11 = sample(x1, y1, c);
                const float a0 = a00 + (a01 - a00) * tx;
                const float a1 = a10 + (a11 - a10) * tx;
                rgb[c] = a0 + (a1 - a0) * ty;
            }
            float nr, ng, nb;
            normalizePixel(rgb[0], rgb[1], rgb[2], norm, nr, ng, nb);
            const std::size_t pix = static_cast<std::size_t>(y) * dw + x;
            if (nchw) {
                out[pix] = nr;
                out[plane + pix] = ng;
                out[2 * plane + pix] = nb;
            } else {
                out[pix * 3u + 0] = nr;
                out[pix * 3u + 1] = ng;
                out[pix * 3u + 2] = nb;
            }
        }
    }
}

void floatToFp16(const std::vector<float>& src, std::vector<Ort::Float16_t>& dst) {
    dst.resize(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        dst[i] = toF16(src[i]);
    }
}

void upsampleBilinear(const float* src, std::uint32_t sw, std::uint32_t sh, std::vector<float>& dst,
                      std::uint32_t dw, std::uint32_t dh) {
    dst.resize(static_cast<std::size_t>(dw) * dh);
    if (sw == 0 || sh == 0) {
        std::fill(dst.begin(), dst.end(), 0.0f);
        return;
    }
    if (sw == dw && sh == dh) {
        std::memcpy(dst.data(), src, dst.size() * sizeof(float));
        return;
    }
    for (std::uint32_t y = 0; y < dh; ++y) {
        const float fy =
            (static_cast<float>(y) + 0.5f) * static_cast<float>(sh) / static_cast<float>(dh) - 0.5f;
        const int y0 = std::max(0, static_cast<int>(std::floor(fy)));
        const int y1 = std::min(static_cast<int>(sh) - 1, y0 + 1);
        const float ty = fy - static_cast<float>(y0);
        for (std::uint32_t x = 0; x < dw; ++x) {
            const float fx =
                (static_cast<float>(x) + 0.5f) * static_cast<float>(sw) / static_cast<float>(dw) -
                0.5f;
            const int x0 = std::max(0, static_cast<int>(std::floor(fx)));
            const int x1 = std::min(static_cast<int>(sw) - 1, x0 + 1);
            const float tx = fx - static_cast<float>(x0);
            const float a00 = src[static_cast<std::size_t>(y0) * sw + x0];
            const float a01 = src[static_cast<std::size_t>(y0) * sw + x1];
            const float a10 = src[static_cast<std::size_t>(y1) * sw + x0];
            const float a11 = src[static_cast<std::size_t>(y1) * sw + x1];
            const float a0 = a00 + (a01 - a00) * tx;
            const float a1 = a10 + (a11 - a10) * tx;
            dst[static_cast<std::size_t>(y) * dw + x] = a0 + (a1 - a0) * ty;
        }
    }
}

void applyActivation(std::vector<float>& map, VisionActivation act) {
    if (map.empty()) {
        return;
    }
    VisionActivation use = act;
    if (use == VisionActivation::Auto) {
        float mn = map[0];
        float mx = map[0];
        for (float v : map) {
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        if (mn >= -0.02f && mx <= 1.02f) {
            use = VisionActivation::None;
        } else if (mx > 1.5f || mn < -0.1f) {
            // Logits or unbounded depth-like — prefer sigmoid for typical matte logits;
            // depth models usually want MinMax (caller should set MinMax explicitly).
            use = VisionActivation::Sigmoid;
        } else {
            use = VisionActivation::MinMax;
        }
    }

    switch (use) {
    case VisionActivation::Sigmoid:
        for (float& v : map) {
            v = 1.0f / (1.0f + std::exp(-v));
        }
        break;
    case VisionActivation::MinMax: {
        float mn = map[0];
        float mx = map[0];
        for (float v : map) {
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        const float range = std::max(1e-6f, mx - mn);
        for (float& v : map) {
            v = (v - mn) / range;
        }
        break;
    }
    case VisionActivation::None:
    default:
        for (float& v : map) {
            v = std::clamp(v, 0.0f, 1.0f);
        }
        break;
    }
}

bool isDynamicDim(std::int64_t d) { return d <= 0; }

} // namespace

struct MapOnnxSession::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "genesis_map_onnx"};
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> session;
    Ort::AllocatorWithDefaultOptions allocator;

    std::vector<float> input_f32;
    std::vector<Ort::Float16_t> input_f16;
    std::vector<float> map_net; // network-res single channel
    std::vector<float> colour_net; // network-res RGB planar or interleaved scratch
};

MapOnnxSession::~MapOnnxSession() {
    delete impl_;
    impl_ = nullptr;
    ready_ = false;
}

void MapOnnxSession::reset() {
    last_infer_w_ = 0;
    last_infer_h_ = 0;
    last_infer_ms_ = 0.0;
    if (impl_) {
        impl_->input_f32.clear();
        impl_->input_f16.clear();
        impl_->map_net.clear();
        impl_->colour_net.clear();
    }
}

bool MapOnnxSession::load(const std::filesystem::path& model_path, std::string* out_error,
                          bool prefer_cpu, int dml_device_id, std::uint64_t adapter_luid) {
    delete impl_;
    impl_ = nullptr;
    ready_ = false;
    colour_out_ = false;
    nchw_ = true;
    input_fp16_ = false;
    output_fp16_ = false;
    ep_name_.clear();
    input_name_.clear();
    output_name_.clear();
    model_h_ = 0;
    model_w_ = 0;
    model_c_in_ = 3;
    model_c_out_ = 1;
    last_infer_ms_ = 0.0;

    if (model_path.empty() || !std::filesystem::exists(model_path)) {
        if (out_error) {
            *out_error = "Map ONNX model not found: " + model_path.string();
        }
        return false;
    }

    auto try_create = [&](Ort::SessionOptions& opts, const char* ep_label) -> bool {
        try {
#if defined(_WIN32)
            const std::wstring wpath = model_path.wstring();
            impl_->session = std::make_unique<Ort::Session>(impl_->env, wpath.c_str(), opts);
#else
            impl_->session =
                std::make_unique<Ort::Session>(impl_->env, model_path.string().c_str(), opts);
#endif
            ep_name_ = ep_label;
            return true;
        } catch (const std::exception&) {
            impl_->session.reset();
            return false;
        }
    };

    try {
        impl_ = new Impl();
        std::string dml_err;

#if defined(_WIN32) && defined(GENESIS_HAS_ORT_DIRECTML)
        if (!prefer_cpu) {
            std::vector<int> candidates;
            auto push_unique = [&](int id) {
                if (id < 0) {
                    return;
                }
                for (int c : candidates) {
                    if (c == id) {
                        return;
                    }
                }
                candidates.push_back(id);
            };
            if (dml_device_id >= 0) {
                push_unique(dml_device_id);
            }
            (void)adapter_luid;
            for (int id = 0; id < 8; ++id) {
                push_unique(id);
            }

            for (int id : candidates) {
                Ort::SessionOptions dml_opts;
                dml_opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
                dml_opts.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
                dml_opts.DisableMemPattern();
                dml_opts.SetIntraOpNumThreads(1);
                dml_opts.SetInterOpNumThreads(1);
                try {
                    std::unordered_map<std::string, std::string> provider_options{
                        {"device_id", std::to_string(id)},
                        {"performance_preference", "high_performance"},
                    };
                    dml_opts.AppendExecutionProvider("DML", provider_options);
                    const std::string label = "DML:" + std::to_string(id);
                    if (try_create(dml_opts, label.c_str())) {
                        break;
                    }
                } catch (const std::exception& ex) {
                    dml_err = ex.what();
                }
            }
        }
#else
        (void)prefer_cpu;
        (void)dml_device_id;
        (void)adapter_luid;
#endif

        if (!impl_->session) {
            const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
            const int threads = static_cast<int>(std::min(8u, std::max(2u, hw)));
            Ort::SessionOptions cpu_opts;
            cpu_opts.SetIntraOpNumThreads(threads);
            cpu_opts.SetInterOpNumThreads(1);
            cpu_opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            if (!try_create(cpu_opts, "CPU")) {
                delete impl_;
                impl_ = nullptr;
                if (out_error) {
                    *out_error =
                        prefer_cpu ? "ORT load failed (CPU)" : "ORT load failed (CPU and DML)";
                }
                return false;
            }
        }

        // Inspect first input / first output for layout + fixed size + dtype.
        {
            auto* session = impl_->session.get();
            const std::size_t nin = session->GetInputCount();
            const std::size_t nout = session->GetOutputCount();
            if (nin < 1 || nout < 1) {
                delete impl_;
                impl_ = nullptr;
                if (out_error) {
                    *out_error = "Map ONNX model has no inputs/outputs";
                }
                return false;
            }

            {
                auto in_name = session->GetInputNameAllocated(0, impl_->allocator);
                input_name_ = in_name.get();
                auto type_info = session->GetInputTypeInfo(0);
                auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
                const auto shape = tensor_info.GetShape();
                const auto elem = tensor_info.GetElementType();
                input_fp16_ = (elem == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16);

                if (shape.size() == 4) {
                    // NCHW: N,C,H,W  or  NHWC: N,H,W,C
                    if (!isDynamicDim(shape[1]) && shape[1] <= 4 &&
                        (isDynamicDim(shape[3]) || shape[3] > 4 || shape[1] == 3 || shape[1] == 1)) {
                        // Prefer NCHW when C is small/fixed at dim1
                        nchw_ = true;
                        model_c_in_ = isDynamicDim(shape[1]) ? 3 : shape[1];
                        model_h_ = isDynamicDim(shape[2]) ? 0 : shape[2];
                        model_w_ = isDynamicDim(shape[3]) ? 0 : shape[3];
                    } else if (!isDynamicDim(shape[3]) && shape[3] <= 4) {
                        nchw_ = false;
                        model_c_in_ = shape[3];
                        model_h_ = isDynamicDim(shape[1]) ? 0 : shape[1];
                        model_w_ = isDynamicDim(shape[2]) ? 0 : shape[2];
                    } else {
                        // Ambiguous dynamic — default NCHW RGB
                        nchw_ = true;
                        model_c_in_ = 3;
                        model_h_ = (!isDynamicDim(shape[2])) ? shape[2] : 0;
                        model_w_ = (!isDynamicDim(shape[3])) ? shape[3] : 0;
                        if (!isDynamicDim(shape[1]) && shape[1] > 4) {
                            // Likely NHWC with dynamic C? treat as NHWC H,W known
                            nchw_ = false;
                            model_h_ = isDynamicDim(shape[1]) ? 0 : shape[1];
                            model_w_ = isDynamicDim(shape[2]) ? 0 : shape[2];
                            model_c_in_ = isDynamicDim(shape[3]) ? 3 : shape[3];
                        }
                    }
                }
            }

            {
                auto out_name = session->GetOutputNameAllocated(0, impl_->allocator);
                output_name_ = out_name.get();
                auto type_info = session->GetOutputTypeInfo(0);
                auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
                const auto shape = tensor_info.GetShape();
                const auto elem = tensor_info.GetElementType();
                output_fp16_ = (elem == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16);

                std::int64_t cout = 1;
                if (shape.size() == 4) {
                    if (nchw_) {
                        cout = isDynamicDim(shape[1]) ? 1 : shape[1];
                    } else {
                        cout = isDynamicDim(shape[3]) ? 1 : shape[3];
                    }
                } else if (shape.size() == 3) {
                    // N,H,W single-channel
                    cout = 1;
                } else if (shape.size() == 2) {
                    cout = 1;
                }
                model_c_out_ = cout;
                colour_out_ = (cout >= 3);
            }
        }

        ready_ = true;
        if (out_error) {
            if (prefer_cpu && ep_name_ == "CPU") {
                *out_error = "map-onnx CPU loaded (prefer_cpu): " + model_path.string();
            } else if (ep_name_ == "CPU" && !dml_err.empty()) {
                *out_error =
                    "map-onnx CPU loaded (DML failed: " + dml_err + "): " + model_path.string();
            } else if (ep_name_ == "CPU") {
                *out_error = "map-onnx CPU loaded (DML unavailable): " + model_path.string();
            } else {
                *out_error = "map-onnx " + ep_name_ + " loaded: " + model_path.string();
            }
        }
        return true;
    } catch (const std::exception& ex) {
        delete impl_;
        impl_ = nullptr;
        if (out_error) {
            *out_error = std::string("ORT load failed: ") + ex.what();
        }
        return false;
    }
}

namespace {

bool resolveInferSize(const MapOnnxSession& /*self*/, std::int64_t model_h, std::int64_t model_w,
                      const MapOnnxConfig& config, std::uint32_t src_w, std::uint32_t src_h,
                      std::uint32_t& iw, std::uint32_t& ih) {
    if (config.size_override > 0) {
        iw = static_cast<std::uint32_t>(config.size_override);
        ih = iw;
    } else if (model_h > 0 && model_w > 0) {
        iw = static_cast<std::uint32_t>(model_w);
        ih = static_cast<std::uint32_t>(model_h);
    } else {
        const std::uint32_t max_side = std::max<std::uint32_t>(64u, config.max_side);
        iw = src_w;
        ih = src_h;
        if (iw > max_side || ih > max_side) {
            const float scale =
                static_cast<float>(max_side) / static_cast<float>(std::max(iw, ih));
            iw = std::max(1u, static_cast<std::uint32_t>(std::lround(iw * scale)));
            ih = std::max(1u, static_cast<std::uint32_t>(std::lround(ih * scale)));
        }
    }
    iw = std::max(1u, iw);
    ih = std::max(1u, ih);
    return true;
}

} // namespace

bool MapOnnxSession::inferMap(const std::uint8_t* in_rgba, std::uint32_t width, std::uint32_t height,
                              const MapOnnxConfig& config, std::vector<float>& out_map,
                              std::string* out_error) {
    if (!ready_ || !impl_ || !impl_->session || !in_rgba || width == 0 || height == 0) {
        if (out_error) {
            *out_error = "Map ONNX session not ready";
        }
        return false;
    }
    if (colour_out_ && model_c_out_ >= 3) {
        // Still allow map extract from first channel of colour models.
    }

    std::uint32_t iw = 0;
    std::uint32_t ih = 0;
    resolveInferSize(*this, model_h_, model_w_, config, width, height, iw, ih);
    last_infer_w_ = iw;
    last_infer_h_ = ih;

    try {
        const auto t0 = std::chrono::steady_clock::now();

        preprocessBilinear(in_rgba, width, height, impl_->input_f32, iw, ih, nchw_, config.norm);

        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 4> shape{};
        if (nchw_) {
            shape = {1, model_c_in_, static_cast<int64_t>(ih), static_cast<int64_t>(iw)};
        } else {
            shape = {1, static_cast<int64_t>(ih), static_cast<int64_t>(iw), model_c_in_};
        }

        Ort::Value input_tensor{nullptr};
        if (input_fp16_) {
            floatToFp16(impl_->input_f32, impl_->input_f16);
            input_tensor = Ort::Value::CreateTensor<Ort::Float16_t>(
                memory_info, impl_->input_f16.data(), impl_->input_f16.size(), shape.data(),
                shape.size());
        } else {
            input_tensor = Ort::Value::CreateTensor<float>(
                memory_info, impl_->input_f32.data(), impl_->input_f32.size(), shape.data(),
                shape.size());
        }

        const char* in_names[] = {input_name_.c_str()};
        const char* out_names[] = {output_name_.c_str()};
        auto outputs = impl_->session->Run(Ort::RunOptions{nullptr}, in_names, &input_tensor, 1,
                                           out_names, 1);

        auto& out = outputs[0];
        const auto info = out.GetTensorTypeAndShapeInfo();
        const auto out_shape = info.GetShape();
        std::uint32_t oh = ih;
        std::uint32_t ow = iw;
        std::int64_t oc = 1;
        bool out_nchw = nchw_;
        if (out_shape.size() == 4) {
            if (!isDynamicDim(out_shape[1]) && out_shape[1] <= 4 &&
                (isDynamicDim(out_shape[3]) || out_shape[3] > 4)) {
                out_nchw = true;
                oc = isDynamicDim(out_shape[1]) ? 1 : out_shape[1];
                oh = static_cast<std::uint32_t>(out_shape[2]);
                ow = static_cast<std::uint32_t>(out_shape[3]);
            } else {
                out_nchw = false;
                oc = isDynamicDim(out_shape[3]) ? 1 : out_shape[3];
                oh = static_cast<std::uint32_t>(out_shape[1]);
                ow = static_cast<std::uint32_t>(out_shape[2]);
            }
        } else if (out_shape.size() == 3) {
            // N,H,W
            oh = static_cast<std::uint32_t>(out_shape[1]);
            ow = static_cast<std::uint32_t>(out_shape[2]);
            oc = 1;
            out_nchw = true;
        } else if (out_shape.size() == 2) {
            oh = static_cast<std::uint32_t>(out_shape[0]);
            ow = static_cast<std::uint32_t>(out_shape[1]);
            oc = 1;
        }

        const std::size_t plane = static_cast<std::size_t>(ow) * oh;
        impl_->map_net.resize(plane);

        auto readF = [&](std::size_t idx) -> float {
            if (output_fp16_) {
                return fromF16(out.GetTensorData<Ort::Float16_t>()[idx]);
            }
            return out.GetTensorData<float>()[idx];
        };

        if (oc <= 1) {
            for (std::size_t i = 0; i < plane; ++i) {
                impl_->map_net[i] = readF(i);
            }
        } else if (out_nchw) {
            // First channel
            for (std::size_t i = 0; i < plane; ++i) {
                impl_->map_net[i] = readF(i);
            }
        } else {
            for (std::size_t i = 0; i < plane; ++i) {
                impl_->map_net[i] = readF(i * static_cast<std::size_t>(oc));
            }
        }

        applyActivation(impl_->map_net, config.activation);
        upsampleBilinear(impl_->map_net.data(), ow, oh, out_map, width, height);

        const auto t1 = std::chrono::steady_clock::now();
        last_infer_ms_ = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return true;
    } catch (const std::exception& ex) {
        if (out_error) {
            *out_error = std::string("Map ONNX inferMap failed: ") + ex.what();
        }
        return false;
    }
}

bool MapOnnxSession::inferColour(const std::uint8_t* in_rgba, std::uint32_t width,
                                 std::uint32_t height, const MapOnnxConfig& config,
                                 std::vector<std::uint8_t>& out_rgba, std::string* out_error) {
    if (!ready_ || !impl_ || !impl_->session || !in_rgba || width == 0 || height == 0) {
        if (out_error) {
            *out_error = "Map ONNX session not ready";
        }
        return false;
    }

    std::uint32_t iw = 0;
    std::uint32_t ih = 0;
    resolveInferSize(*this, model_h_, model_w_, config, width, height, iw, ih);
    last_infer_w_ = iw;
    last_infer_h_ = ih;

    try {
        const auto t0 = std::chrono::steady_clock::now();

        preprocessBilinear(in_rgba, width, height, impl_->input_f32, iw, ih, nchw_, config.norm);

        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 4> shape{};
        if (nchw_) {
            shape = {1, model_c_in_, static_cast<int64_t>(ih), static_cast<int64_t>(iw)};
        } else {
            shape = {1, static_cast<int64_t>(ih), static_cast<int64_t>(iw), model_c_in_};
        }

        Ort::Value input_tensor{nullptr};
        if (input_fp16_) {
            floatToFp16(impl_->input_f32, impl_->input_f16);
            input_tensor = Ort::Value::CreateTensor<Ort::Float16_t>(
                memory_info, impl_->input_f16.data(), impl_->input_f16.size(), shape.data(),
                shape.size());
        } else {
            input_tensor = Ort::Value::CreateTensor<float>(
                memory_info, impl_->input_f32.data(), impl_->input_f32.size(), shape.data(),
                shape.size());
        }

        const char* in_names[] = {input_name_.c_str()};
        const char* out_names[] = {output_name_.c_str()};
        auto outputs = impl_->session->Run(Ort::RunOptions{nullptr}, in_names, &input_tensor, 1,
                                           out_names, 1);

        auto& out = outputs[0];
        const auto info = out.GetTensorTypeAndShapeInfo();
        const auto out_shape = info.GetShape();
        std::uint32_t oh = ih;
        std::uint32_t ow = iw;
        std::int64_t oc = 3;
        bool out_nchw = nchw_;
        if (out_shape.size() == 4) {
            if (!isDynamicDim(out_shape[1]) && out_shape[1] <= 4) {
                out_nchw = true;
                oc = out_shape[1];
                oh = static_cast<std::uint32_t>(out_shape[2]);
                ow = static_cast<std::uint32_t>(out_shape[3]);
            } else {
                out_nchw = false;
                oc = isDynamicDim(out_shape[3]) ? 3 : out_shape[3];
                oh = static_cast<std::uint32_t>(out_shape[1]);
                ow = static_cast<std::uint32_t>(out_shape[2]);
            }
        }
        oc = std::max<std::int64_t>(1, oc);

        auto readF = [&](std::size_t idx) -> float {
            if (output_fp16_) {
                return fromF16(out.GetTensorData<Ort::Float16_t>()[idx]);
            }
            return out.GetTensorData<float>()[idx];
        };

        const std::size_t plane = static_cast<std::size_t>(ow) * oh;
        // Planar RGB at network res, then upsample each channel.
        std::vector<float> r_net(plane), g_net(plane), b_net(plane);
        for (std::size_t i = 0; i < plane; ++i) {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            if (out_nchw) {
                r = readF(i);
                g = (oc > 1) ? readF(plane + i) : r;
                b = (oc > 2) ? readF(2 * plane + i) : r;
            } else {
                r = readF(i * static_cast<std::size_t>(oc) + 0);
                g = (oc > 1) ? readF(i * static_cast<std::size_t>(oc) + 1) : r;
                b = (oc > 2) ? readF(i * static_cast<std::size_t>(oc) + 2) : r;
            }
            // Assume network emits roughly [0,1]; clamp. ImageNet-style outs are uncommon.
            r_net[i] = std::clamp(r, 0.0f, 1.0f);
            g_net[i] = std::clamp(g, 0.0f, 1.0f);
            b_net[i] = std::clamp(b, 0.0f, 1.0f);
        }

        std::vector<float> r_full, g_full, b_full;
        upsampleBilinear(r_net.data(), ow, oh, r_full, width, height);
        upsampleBilinear(g_net.data(), ow, oh, g_full, width, height);
        upsampleBilinear(b_net.data(), ow, oh, b_full, width, height);

        out_rgba.resize(static_cast<std::size_t>(width) * height * 4u);
        for (std::size_t i = 0; i < r_full.size(); ++i) {
            out_rgba[i * 4u + 0] =
                static_cast<std::uint8_t>(std::lround(std::clamp(r_full[i], 0.0f, 1.0f) * 255.0f));
            out_rgba[i * 4u + 1] =
                static_cast<std::uint8_t>(std::lround(std::clamp(g_full[i], 0.0f, 1.0f) * 255.0f));
            out_rgba[i * 4u + 2] =
                static_cast<std::uint8_t>(std::lround(std::clamp(b_full[i], 0.0f, 1.0f) * 255.0f));
            out_rgba[i * 4u + 3] = 255;
        }

        const auto t1 = std::chrono::steady_clock::now();
        last_infer_ms_ = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return true;
    } catch (const std::exception& ex) {
        if (out_error) {
            *out_error = std::string("Map ONNX inferColour failed: ") + ex.what();
        }
        return false;
    }
}

} // namespace genesis

#else // !GENESIS_HAS_ONNXRUNTIME

namespace genesis {

MapOnnxSession::~MapOnnxSession() = default;

bool MapOnnxSession::load(const std::filesystem::path&, std::string* out_error, bool, int,
                          std::uint64_t) {
    if (out_error) {
        *out_error = "ONNX Runtime not enabled (GENESIS_ENABLE_ONNXRUNTIME=OFF)";
    }
    return false;
}

void MapOnnxSession::reset() {}

bool MapOnnxSession::inferMap(const std::uint8_t*, std::uint32_t, std::uint32_t,
                              const MapOnnxConfig&, std::vector<float>&, std::string* out_error) {
    if (out_error) {
        *out_error = "ONNX Runtime not enabled";
    }
    return false;
}

bool MapOnnxSession::inferColour(const std::uint8_t*, std::uint32_t, std::uint32_t,
                                 const MapOnnxConfig&, std::vector<std::uint8_t>&,
                                 std::string* out_error) {
    if (out_error) {
        *out_error = "ONNX Runtime not enabled";
    }
    return false;
}

} // namespace genesis

#endif
