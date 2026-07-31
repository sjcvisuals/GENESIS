#pragma once

#include "runtime/RemoverPipeline.hpp"
#include "rxext_client.h"

#include <cstdint>
#include <memory>

namespace rxext::genesis_remover {

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
    void ensurePipeline();
    void applyMatteSettings();
    void publishOutput();
    void publishVideoIn();

    std::uint64_t adapter_luid_ = 0;
    std::uint64_t pipeline_adapter_luid_ = 0;
    ValueSet settings_;
    ParameterTexture* sampler_ = nullptr;
    ParameterTexture* video_in_ = nullptr;
    ParameterInt* quality_ = nullptr;
    ParameterValue* time_ = nullptr;
    std::unique_ptr<::genesis::RemoverPipeline> pipeline_;
    std::uint32_t width_ = 1920;
    std::uint32_t height_ = 1080;
};

} // namespace rxext::genesis_remover
