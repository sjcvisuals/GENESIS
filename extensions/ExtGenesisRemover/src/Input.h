#pragma once

#include "WinPrefix.h"
#include "rxext_client.h"

#include "D3d11SharedTexture.h"
#include "SharedD3d11.h"
#include "D3d11Passthrough.h"
#include "runtime/RemoverPipeline.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rxext::genesis_remover {

// Live: GPU blit Video In -> shared every frame (proven live in v36).
// Matte: async blit -> private capture -> non-blocking poll/Map; upload cutout to
// private matte RT; blit matte -> shared. Never stall fence with waitGpuIdle/Flush/Map.
class Input : public rxext::InputStream {
public:
    Input(std::shared_ptr<SharedD3d11> d3d, std::uint64_t adapter_luid, ValueSet settings);
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
    void ensureRemover();
    void ensureD3dTextures();
    void applyMatteSettings();
    void publishTargetTexture();
    void publishVideoInTexture();
    void logStatusMaybe(const ::genesis::RemoverFrameDebug& dbg);
    void clearVideoInLink(const char* reason);
    void tryFinishAsyncMatte(::genesis::RemoverFrameDebug& dbg, std::string& error);
    void maybeStartAsyncCapture();

    std::shared_ptr<SharedD3d11> d3d_;
    std::uint64_t adapter_luid_ = 0;
    ValueSet settings_;
    ParameterTexture* sampler_ = nullptr;
    ParameterTexture* video_in_ = nullptr;
    ParameterInt* quality_ = nullptr;
    ParameterValue* time_ = nullptr;
    ParameterValue* frame_counter_ = nullptr;
    ParameterInt* debug_stage_ = nullptr;
    ParameterString* debug_status_ = nullptr;

    std::unique_ptr<::genesis::RemoverPipeline> remover_;
    std::unique_ptr<D3d11SharedTexture> d3d_output_;
    std::unique_ptr<D3d11SharedTexture> d3d_video_in_;
    // Private capture RT: Video In blit target for async CPU readback.
    std::unique_ptr<D3d11SharedTexture> d3d_capture_;
    // Private matte RT: last cutout for display (blit to shared output).
    std::unique_ptr<D3d11SharedTexture> d3d_matte_tex_;
    std::unique_ptr<D3d11Passthrough> passthrough_;
    std::vector<std::uint8_t> d3d_rgba_in_;
    std::vector<std::uint8_t> d3d_rgba_out_;
    std::uint32_t width_ = 1920;
    std::uint32_t height_ = 1080;
    std::uint64_t render_ticks_ = 0;
    std::string last_stage_;
    bool video_in_ready_ = false;
    bool matte_live_ = false;
    std::uint32_t same_mid_frames_ = 0;
    std::array<std::uint8_t, 4> last_mid_rgba_{};
};

} // namespace rxext::genesis_remover
