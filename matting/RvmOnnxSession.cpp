#include "matting/RvmOnnxSession.hpp"

#if defined(GENESIS_HAS_ONNXRUNTIME)

#include "platform/Paths.hpp"

#include <onnxruntime_cxx_api.h>
#include <onnxruntime_float16.h>

#include <algorithm>
#include <array>
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

Ort::Float16_t toF16(float v) {
    return Ort::Float16_t(v);
}

float fromF16(Ort::Float16_t v) {
    return static_cast<float>(v);
}

const std::array<Ort::Float16_t, 256>& byteToF16Lut() {
    static const std::array<Ort::Float16_t, 256> lut = [] {
        std::array<Ort::Float16_t, 256> t{};
        for (int i = 0; i < 256; ++i) {
            t[static_cast<std::size_t>(i)] = toF16(static_cast<float>(i) / 255.0f);
        }
        return t;
    }();
    return lut;
}

void resizeRgbNearest(const std::uint8_t* src_rgba, std::uint32_t sw, std::uint32_t sh,
                      std::vector<Ort::Float16_t>& dst_nchw, std::uint32_t dw, std::uint32_t dh) {
    dst_nchw.resize(static_cast<std::size_t>(3) * dw * dh);
    const auto& lut = byteToF16Lut();
    std::vector<std::uint32_t> xs(dw);
    for (std::uint32_t x = 0; x < dw; ++x) {
        xs[x] = std::min(sw - 1, x * sw / dw);
    }
    Ort::Float16_t* plane0 = dst_nchw.data();
    Ort::Float16_t* plane1 = plane0 + static_cast<std::size_t>(dw) * dh;
    Ort::Float16_t* plane2 = plane1 + static_cast<std::size_t>(dw) * dh;
    for (std::uint32_t y = 0; y < dh; ++y) {
        const std::uint32_t sy = std::min(sh - 1, y * sh / dh);
        const std::uint8_t* row = src_rgba + static_cast<std::size_t>(sy) * sw * 4u;
        const std::size_t row_off = static_cast<std::size_t>(y) * dw;
        for (std::uint32_t x = 0; x < dw; ++x) {
            const std::uint8_t* p = row + static_cast<std::size_t>(xs[x]) * 4u;
            plane0[row_off + x] = lut[p[0]];
            plane1[row_off + x] = lut[p[1]];
            plane2[row_off + x] = lut[p[2]];
        }
    }
}

void upsampleAlphaNearest(const float* src, std::uint32_t sw, std::uint32_t sh, std::vector<float>& dst,
                          std::uint32_t dw, std::uint32_t dh) {
    dst.resize(static_cast<std::size_t>(dw) * dh);
    if (sw == 0 || sh == 0) {
        std::fill(dst.begin(), dst.end(), 0.0f);
        return;
    }
    std::vector<std::uint32_t> xs(dw);
    for (std::uint32_t x = 0; x < dw; ++x) {
        xs[x] = std::min(sw - 1, x * sw / dw);
    }
    for (std::uint32_t y = 0; y < dh; ++y) {
        const std::uint32_t sy = std::min(sh - 1, y * sh / dh);
        const float* src_row = src + static_cast<std::size_t>(sy) * sw;
        float* dst_row = dst.data() + static_cast<std::size_t>(y) * dw;
        for (std::uint32_t x = 0; x < dw; ++x) {
            dst_row[x] = src_row[xs[x]];
        }
    }
}

void upsampleAlphaBilinear(const float* src, std::uint32_t sw, std::uint32_t sh,
                           std::vector<float>& dst, std::uint32_t dw, std::uint32_t dh) {
    dst.resize(static_cast<std::size_t>(dw) * dh);
    if (sw == 0 || sh == 0) {
        std::fill(dst.begin(), dst.end(), 0.0f);
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
                (static_cast<float>(x) + 0.5f) * static_cast<float>(sw) / static_cast<float>(dw) - 0.5f;
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

} // namespace

struct RvmOnnxSession::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "genesis_rvm"};
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> session;
    Ort::AllocatorWithDefaultOptions allocator;

    std::vector<Ort::Float16_t> r1{toF16(0.0f)};
    std::vector<Ort::Float16_t> r2{toF16(0.0f)};
    std::vector<Ort::Float16_t> r3{toF16(0.0f)};
    std::vector<Ort::Float16_t> r4{toF16(0.0f)};
    std::vector<int64_t> r1_shape{1, 1, 1, 1};
    std::vector<int64_t> r2_shape{1, 1, 1, 1};
    std::vector<int64_t> r3_shape{1, 1, 1, 1};
    std::vector<int64_t> r4_shape{1, 1, 1, 1};

    std::vector<Ort::Float16_t> src_nchw;
    std::vector<float> pha_f32;
};

RvmOnnxSession::~RvmOnnxSession() {
    delete impl_;
    impl_ = nullptr;
    ready_ = false;
}

void RvmOnnxSession::reset() {
    if (!impl_) {
        return;
    }
    impl_->r1 = {toF16(0.0f)};
    impl_->r2 = {toF16(0.0f)};
    impl_->r3 = {toF16(0.0f)};
    impl_->r4 = {toF16(0.0f)};
    impl_->r1_shape = {1, 1, 1, 1};
    impl_->r2_shape = {1, 1, 1, 1};
    impl_->r3_shape = {1, 1, 1, 1};
    impl_->r4_shape = {1, 1, 1, 1};
    last_infer_w_ = 0;
    last_infer_h_ = 0;
}

bool RvmOnnxSession::load(const std::filesystem::path& model_path, std::string* out_error,
                          bool prefer_cpu, int dml_device_id, std::uint64_t adapter_luid) {
    delete impl_;
    impl_ = nullptr;
    ready_ = false;
    ep_name_.clear();

    if (model_path.empty() || !std::filesystem::exists(model_path)) {
        if (out_error) {
            *out_error = "RVM model not found: " + model_path.string();
        }
        return false;
    }
    if (isGitLfsPointerFile(model_path)) {
        if (out_error) {
            *out_error =
                "RVM model is a Git LFS pointer (not the model). Run: git lfs pull — then rebuild/install. Path: " +
                model_path.string();
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
            // Build candidate DXGI adapter indices. device_id 0 is often the iGPU on laptops;
            // PIXERA Adapter LUID usually points at the discrete / preview GPU.
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
            (void)adapter_luid; // resolved by caller into dml_device_id when possible
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
                        ready_ = true;
                        if (out_error) {
                            *out_error = "rvm-onnx " + label + " loaded: " + model_path.string();
                        }
                        return true;
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
                *out_error = prefer_cpu ? "ORT load failed (CPU)" : "ORT load failed (CPU and DML)";
            }
            return false;
        }
        ready_ = true;
        if (out_error) {
            if (prefer_cpu) {
                *out_error = "rvm-onnx CPU loaded (prefer_cpu): " + model_path.string();
            } else if (!dml_err.empty()) {
                *out_error = "rvm-onnx CPU loaded (DML failed: " + dml_err + "): " + model_path.string();
            } else {
                *out_error = "rvm-onnx CPU loaded (DML unavailable): " + model_path.string();
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

bool RvmOnnxSession::inferAlpha(const std::uint8_t* in_rgba, std::uint32_t width, std::uint32_t height,
                                const RvmInferParams& params, std::vector<float>& out_alpha,
                                std::string* out_error) {
    if (!ready_ || !impl_ || !in_rgba || width == 0 || height == 0) {
        if (out_error) *out_error = "RVM session not ready";
        return false;
    }

    const float ds = std::clamp(params.downsample_ratio, 0.05f, 1.0f);
    std::uint32_t iw = std::max(1u, static_cast<std::uint32_t>(std::lround(width * ds)));
    std::uint32_t ih = std::max(1u, static_cast<std::uint32_t>(std::lround(height * ds)));
    const std::uint32_t max_side = std::max<std::uint32_t>(64u, params.max_infer_side);
    if (iw > max_side || ih > max_side) {
        const float scale =
            static_cast<float>(max_side) / static_cast<float>(std::max(iw, ih));
        iw = std::max(1u, static_cast<std::uint32_t>(std::lround(iw * scale)));
        ih = std::max(1u, static_cast<std::uint32_t>(std::lround(ih * scale)));
    }
    iw = std::max(16u, (iw + 1u) & ~1u);
    ih = std::max(16u, (ih + 1u) & ~1u);
    last_infer_w_ = iw;
    last_infer_h_ = ih;

    try {
        resizeRgbNearest(in_rgba, width, height, impl_->src_nchw, iw, ih);

        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 4> src_shape{1, 3, static_cast<int64_t>(ih), static_cast<int64_t>(iw)};
        Ort::Value src = Ort::Value::CreateTensor<Ort::Float16_t>(
            memory_info, impl_->src_nchw.data(), impl_->src_nchw.size(), src_shape.data(),
            src_shape.size());

        Ort::Value r1 = Ort::Value::CreateTensor<Ort::Float16_t>(
            memory_info, impl_->r1.data(), impl_->r1.size(), impl_->r1_shape.data(),
            impl_->r1_shape.size());
        Ort::Value r2 = Ort::Value::CreateTensor<Ort::Float16_t>(
            memory_info, impl_->r2.data(), impl_->r2.size(), impl_->r2_shape.data(),
            impl_->r2_shape.size());
        Ort::Value r3 = Ort::Value::CreateTensor<Ort::Float16_t>(
            memory_info, impl_->r3.data(), impl_->r3.size(), impl_->r3_shape.data(),
            impl_->r3_shape.size());
        Ort::Value r4 = Ort::Value::CreateTensor<Ort::Float16_t>(
            memory_info, impl_->r4.data(), impl_->r4.size(), impl_->r4_shape.data(),
            impl_->r4_shape.size());

        float ratio = 1.0f;
        std::array<int64_t, 1> ratio_shape{1};
        Ort::Value ds_tensor =
            Ort::Value::CreateTensor<float>(memory_info, &ratio, 1, ratio_shape.data(),
                                            ratio_shape.size());

        const char* input_names[] = {"src", "r1i", "r2i", "r3i", "r4i", "downsample_ratio"};
        const char* output_names[] = {"fgr", "pha", "r1o", "r2o", "r3o", "r4o"};

        std::array<Ort::Value, 6> inputs{std::move(src), std::move(r1), std::move(r2), std::move(r3),
                                         std::move(r4), std::move(ds_tensor)};

        auto outputs = impl_->session->Run(Ort::RunOptions{nullptr}, input_names, inputs.data(),
                                           inputs.size(), output_names, 6);

        auto& pha = outputs[1];
        const auto pha_info = pha.GetTensorTypeAndShapeInfo();
        const auto pha_shape = pha_info.GetShape();
        if (pha_shape.size() != 4) {
            if (out_error) *out_error = "unexpected pha rank";
            return false;
        }
        const std::uint32_t ph = static_cast<std::uint32_t>(pha_shape[2]);
        const std::uint32_t pw = static_cast<std::uint32_t>(pha_shape[3]);
        const Ort::Float16_t* pha_data = pha.GetTensorData<Ort::Float16_t>();
        impl_->pha_f32.resize(static_cast<std::size_t>(ph) * pw);
        for (std::size_t i = 0; i < impl_->pha_f32.size(); ++i) {
            impl_->pha_f32[i] = fromF16(pha_data[i]);
        }
        if (params.bilinear_upsample) {
            upsampleAlphaBilinear(impl_->pha_f32.data(), pw, ph, out_alpha, width, height);
        } else {
            upsampleAlphaNearest(impl_->pha_f32.data(), pw, ph, out_alpha, width, height);
        }

        auto captureRec = [](Ort::Value& value, std::vector<Ort::Float16_t>& storage,
                             std::vector<int64_t>& shape) {
            const auto info = value.GetTensorTypeAndShapeInfo();
            shape = info.GetShape();
            const std::size_t count = info.GetElementCount();
            const Ort::Float16_t* data = value.GetTensorData<Ort::Float16_t>();
            storage.assign(data, data + count);
        };
        captureRec(outputs[2], impl_->r1, impl_->r1_shape);
        captureRec(outputs[3], impl_->r2, impl_->r2_shape);
        captureRec(outputs[4], impl_->r3, impl_->r3_shape);
        captureRec(outputs[5], impl_->r4, impl_->r4_shape);
        return true;
    } catch (const std::exception& ex) {
        if (out_error) {
            *out_error = std::string("RVM infer failed: ") + ex.what();
        }
        return false;
    }
}

} // namespace genesis

#else // !GENESIS_HAS_ONNXRUNTIME

namespace genesis {

RvmOnnxSession::~RvmOnnxSession() = default;

bool RvmOnnxSession::load(const std::filesystem::path&, std::string* out_error, bool, int,
                          std::uint64_t) {
    if (out_error) {
        *out_error = "ONNX Runtime not enabled (GENESIS_ENABLE_ONNXRUNTIME=OFF)";
    }
    return false;
}

void RvmOnnxSession::reset() {}

bool RvmOnnxSession::inferAlpha(const std::uint8_t*, std::uint32_t, std::uint32_t, const RvmInferParams&,
                                std::vector<float>&, std::string* out_error) {
    if (out_error) *out_error = "ONNX Runtime not enabled";
    return false;
}

} // namespace genesis

#endif
