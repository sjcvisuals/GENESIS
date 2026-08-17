#pragma once

#include "WinPrefix.h"
#include "rxext_client.h"

#include "runtime/RemoverPipeline.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace rxext::genesis_remover {

// Shared-Vulkan Remover (same path as ExtGenesis Background Remover mode).
// Host download_texture on Video In hangs in PIXERA (stuck blue) — do not use it.
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
    void publishPlaceholderSampler();
    bool tryEnsureGpuPipeline();
    void applyMatteSettings();
    void publishSharedOutput();
    void publishVideoIn();
    void enableProcessing();
    void setDebugStatus(const std::string& text);
    void logStatusMaybe(const ::genesis::RemoverFrameDebug& dbg);

    std::uint64_t adapter_luid_ = 0;
    std::uint64_t pipeline_adapter_luid_ = 0;
    ValueSet settings_;
    ParameterTexture* sampler_ = nullptr;
    ParameterTexture* video_in_ = nullptr;
    ParameterInt* quality_ = nullptr;
    ParameterValue* time_ = nullptr;
    ParameterValue* frame_counter_ = nullptr;
    ParameterString* debug_status_ = nullptr;
    ParameterInt* debug_stage_ = nullptr;

    std::unique_ptr<::genesis::RemoverPipeline> pipeline_;
    std::uint32_t width_ = 1920;
    std::uint32_t height_ = 1080;
    bool params_registered_ = false;
    bool placeholder_published_ = false;
    bool shared_output_published_ = false;
    bool processing_enabled_ = false;
    bool gpu_failed_ = false;
    bool matte_live_ = false;
    bool video_in_published_ = false;
    std::uint64_t render_ticks_ = 0;
    std::string last_stage_;
    std::string last_status_;
};

} // namespace rxext::genesis_remover
