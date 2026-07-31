#pragma once

#include "WinPrefix.h"

// Parse RenderBridge headers before D3D/Windows (ExtSampleD3D order).
#include "rxext_client.h"

#include "d3d11/ComPointer.h"
#include "d3d11/d3d11.h"
#include "matting/AsyncMatteWorker.hpp"
#include "matting/BgRemover.hpp"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

struct ID3D11Texture2D;

namespace rxext::genesis_remover {

class Input : public rxext::InputStream {
public:
    Input(std::shared_ptr<d3d11::Device> d3d_device, ValueSet settings);
    ~Input() override;

    bool initialize() noexcept override;
    bool update_settings(ValueSet settings) noexcept override;
    ValueSet get_state() noexcept override;
    string get_property(string_view name) noexcept override;
    bool update() noexcept override;
    RenderResult render() noexcept override;

private:
    std::unique_ptr<d3d11::Texture> createSharedTexture(ParameterTexture& parameter,
                                                        TextureDesc desc,
                                                        d3d11::Texture::Options options);
    void ensureStaging(std::uint32_t width, std::uint32_t height, DXGI_FORMAT format);
    bool readVideoInRgba(std::vector<std::uint8_t>& out_rgba);
    bool writeOutputRgba(const std::uint8_t* rgba);
    void applyMatteSettings();
    void touchHostClock();
    // ExtSampleD3D-identical: clear + Quad blit. Visibility does NOT depend on source alpha.
    void blitSampleD3D();
    void clearOutput(float r, float g, float b, float a);
    void logRenderPathOnce(const char* path);
    void logMatteStatusMaybe();

    std::shared_ptr<d3d11::Device> d3d_device_;
    ValueSet settings_;
    std::unique_ptr<d3d11::Quad> quad_;

    ParameterTexture* sampler_ = nullptr;
    ParameterTexture* video_in_param_ = nullptr;
    ParameterInt* quality_ = nullptr;
    ParameterValue* time_ = nullptr;
    ParameterValue* frame_counter_ = nullptr;

    std::unique_ptr<d3d11::Texture> output_tex_;
    std::unique_ptr<d3d11::Texture> video_in_tex_;
    Format video_in_format_ = Format::R8G8B8A8_UNORM;

    ComPointer<ID3D11Texture2D> staging_read_;
    ComPointer<ID3D11Texture2D> staging_write_;
    std::uint32_t staging_w_ = 0;
    std::uint32_t staging_h_ = 0;
    DXGI_FORMAT staging_format_ = DXGI_FORMAT_UNKNOWN;

    ::genesis::AsyncMatteWorker async_;
    ::genesis::BgRemoverSettings matte_settings_{};
    bool async_started_ = false;
    bool matte_settings_applied_ = false;
    // Only advertise applies_alpha after a covered matte frame was written.
    bool matte_live_ = false;

    std::vector<std::uint8_t> scratch_in_;
    std::vector<std::uint8_t> scratch_out_;
    std::uint32_t width_ = 1920;
    std::uint32_t height_ = 1080;

    std::string last_render_path_;
    std::string last_matte_status_;
    std::uint64_t render_ticks_ = 0;
    std::uint64_t last_logged_completed_ = 0;
};

} // namespace rxext::genesis_remover
