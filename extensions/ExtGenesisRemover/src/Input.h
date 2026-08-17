#pragma once

#include "WinPrefix.h"
#include "rxext_client.h"

#include "D3d11VideoIn.h"
#include "SharedD3d11.h"
#include "runtime/RemoverPipeline.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rxext::genesis_remover {

// Output sampler: Vulkan OPAQUE_WIN32.
// Video In: D3D11_IMAGE on Device's SharedD3d11 (+ Device D3D_FENCE timeline).
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
    void ensureD3dVideoIn();
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
    std::unique_ptr<D3d11VideoIn> d3d_video_in_;
    std::vector<std::uint8_t> d3d_rgba_;
    std::uint32_t width_ = 1920;
    std::uint32_t height_ = 1080;
    std::uint64_t render_ticks_ = 0;
    std::string last_stage_;
    bool video_in_ready_ = false;
};

} // namespace rxext::genesis_remover
