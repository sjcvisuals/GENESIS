#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ComPointer.h"
#include "d3d11/d3d11.h"
#include "matting/AsyncMatteWorker.hpp"
#include "matting/BgRemover.hpp"
#include "rxext_client.h"

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
    void ensureStaging(std::uint32_t width, std::uint32_t height);
    bool readVideoInRgba(std::vector<std::uint8_t>& out_rgba);
    bool writeOutputRgba(const std::uint8_t* rgba);
    void applyMatteSettings();
    void blitPassthrough();
    void clearOutputTransparent();

    std::shared_ptr<d3d11::Device> d3d_device_;
    ValueSet settings_;
    std::unique_ptr<d3d11::Quad> quad_;

    ParameterTexture* sampler_ = nullptr;
    ParameterTexture* video_in_param_ = nullptr;
    ParameterInt* quality_ = nullptr;
    ParameterValue* time_ = nullptr;

    std::unique_ptr<d3d11::Texture> output_tex_;
    std::unique_ptr<d3d11::Texture> video_in_tex_;

    ComPointer<ID3D11Texture2D> staging_read_;
    ComPointer<ID3D11Texture2D> staging_write_;
    std::uint32_t staging_w_ = 0;
    std::uint32_t staging_h_ = 0;

    ::genesis::AsyncMatteWorker async_;
    ::genesis::BgRemoverSettings matte_settings_{};
    bool async_started_ = false;

    std::vector<std::uint8_t> scratch_in_;
    std::vector<std::uint8_t> scratch_out_;
    std::uint32_t width_ = 1920;
    std::uint32_t height_ = 1080;
};

} // namespace rxext::genesis_remover
