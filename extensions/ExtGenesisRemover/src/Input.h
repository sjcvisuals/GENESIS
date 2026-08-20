#pragma once

#include "WinPrefix.h"
#include "rxext_client.h"

#include "D3d11Passthrough.h"
#include "D3d11SharedTexture.h"
#include "MatteHostClient.h"
#include "RemoverFrameDebug.h"
#include "SharedD3d11.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rxext::genesis_remover {

// v49: live Video In each frame, matte alpha from OOP DirectML host.
// ORT must not load in the PIXERA process (v39–v42 froze the preview).
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
    void ensureD3dTextures();
    void ensureMatteHost();
    void publishTargetTexture();
    void publishVideoInTexture();
    void logStatusMaybe(const ::genesis::RemoverFrameDebug& dbg);
    void clearVideoInLink(const char* reason);

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

    std::unique_ptr<D3d11SharedTexture> d3d_output_;
    std::unique_ptr<D3d11SharedTexture> d3d_video_in_;
    std::unique_ptr<D3d11SharedTexture> d3d_capture_share_;
    std::unique_ptr<D3d11SharedTexture> d3d_matte_tex_;
    std::unique_ptr<D3d11Passthrough> passthrough_;
    std::unique_ptr<MatteHostClient> matte_host_;
    std::vector<std::uint8_t> host_matte_rgba_;
    std::uint32_t width_ = 1920;
    std::uint32_t height_ = 1080;
    std::uint64_t render_ticks_ = 0;
    std::uint64_t last_capture_kick_tick_ = 0;
    std::string last_stage_;
    bool video_in_ready_ = false;
    bool matte_live_ = false;
};

} // namespace rxext::genesis_remover
