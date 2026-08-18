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

// GPU blit Video In → shared output (live). Map a private RGBA8 RT only (never Map
// Video In or shared output — that froze PIXERA on one frame). Matte uploads go to
// the private RT then blit → shared (no CopyResource on the share PIXERA samples).
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
    // Private (non-shared) RGBA8 RT: blit here for CPU Map so we never CopyResource the
    // shared output PIXERA is displaying (that froze the feed on a single frame).
    std::unique_ptr<D3d11SharedTexture> d3d_matte_src_;
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

    void clearVideoInLink(const char* reason);
};

} // namespace rxext::genesis_remover
