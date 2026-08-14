#pragma once

#include "WinPrefix.h"
#include "rxext_client.h"

#include "matting/AsyncMatteWorker.hpp"
#include "matting/BgRemover.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace rxext::genesis_remover {

// Host-texture path (ExtSampleShader Video In + ExtSampleCPU sampler upload).
// D3D11/Vulkan shared Video In stayed black with live PIXERA inputs.
class Input : public rxext::InputStream {
public:
    explicit Input(ValueSet settings);
    ~Input() override;

    bool initialize() noexcept override;
    bool update_settings(ValueSet settings) noexcept override;
    ValueSet get_state() noexcept override;
    string get_property(string_view name) noexcept override;
    bool update() noexcept override;
    RenderResult render() noexcept override;

private:
    void registerParameters();
    void ensureSampler(std::uint32_t width, std::uint32_t height);
    void ensureVideoInHostTexture();
    void requestVideoDownload();
    void applyMatteSettings();
    void uploadOutputRgba(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height);
    void touchHostClock();
    void logMatteStatusMaybe();

    ValueSet settings_;
    ParameterTexture* sampler_ = nullptr;
    ParameterTexture* video_in_ = nullptr;
    ParameterInt* quality_ = nullptr;
    ParameterValue* time_ = nullptr;
    ParameterValue* frame_counter_ = nullptr;

    ::genesis::AsyncMatteWorker async_;
    ::genesis::BgRemoverSettings matte_settings_{};
    bool async_started_ = false;
    bool matte_settings_applied_ = false;
    bool matte_live_ = false;
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
    // Downloads only after WE created a host Video In texture + cooldown.
    bool video_in_allocated_ = false;
    bool video_in_download_allowed_ = false;
    std::uint32_t download_cooldown_frames_ = 0;
    std::atomic<std::uint64_t> video_in_generation_{0};
    bool baseline_captured_ = false;
    int baseline_w_ = 0;
    int baseline_h_ = 0;
    std::string baseline_format_;
    bool same_res_allocate_tried_ = false;

    std::vector<std::uint8_t> scratch_out_;
    std::vector<std::uint8_t> last_alpha_;
    bool has_last_alpha_ = false;
    std::string last_matte_status_;
    std::uint64_t render_ticks_ = 0;
    std::uint64_t last_logged_completed_ = 0;
    std::string last_render_path_;
};

} // namespace rxext::genesis_remover
