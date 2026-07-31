#pragma once

#include "matting/AsyncMatteWorker.hpp"
#include "matting/BgRemover.hpp"
#include "rxext_client.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace rxext::genesis_remover {

// Host-texture Remover path (ExtSampleShader input + ExtSampleCPU output).
// Avoids Vulkan shared-memory Video In, which stayed black under PIXERA.
class Input : public rxext::InputStream {
public:
    Input(std::uint64_t adapter_luid, ValueSet settings);
    ~Input() override;

    bool initialize() noexcept override;
    bool update_settings(ValueSet settings) noexcept override;
    ValueSet get_state() noexcept override;
    string get_property(string_view name) noexcept override;
    bool update() noexcept override;
    SyncDesc before_render() noexcept override;
    RenderResult render() noexcept override;
    SyncDesc after_render() noexcept override;

private:
    void registerParameters();
    void ensureSampler(std::uint32_t width, std::uint32_t height);
    void ensureVideoInHostTexture();
    void requestVideoDownload();
    void applyMatteSettings();
    void uploadOutputRgba(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height);

    std::uint64_t adapter_luid_ = 0;
    ValueSet settings_;
    ParameterTexture* sampler_ = nullptr;
    ParameterTexture* video_in_ = nullptr;
    ParameterInt* quality_ = nullptr;
    ParameterValue* time_ = nullptr;

    ::genesis::AsyncMatteWorker async_;
    ::genesis::BgRemoverSettings matte_settings_{};
    bool async_started_ = false;
    bool params_registered_ = false;

    std::uint32_t width_ = 1920;
    std::uint32_t height_ = 1080;

    std::mutex frame_mutex_;
    std::vector<std::uint8_t> latest_in_;
    std::uint32_t in_w_ = 0;
    std::uint32_t in_h_ = 0;
    bool has_in_ = false;
    std::atomic<bool> download_pending_{false};
    std::atomic<std::uint64_t> downloads_ok_{0};
    std::atomic<std::uint64_t> downloads_empty_{0};

    std::vector<std::uint8_t> scratch_out_;
};

} // namespace rxext::genesis_remover
