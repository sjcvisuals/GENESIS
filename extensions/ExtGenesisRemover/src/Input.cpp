#include "WinPrefix.h"
#include "Input.h"

#include "ModulePaths.h"
#include "RemoverLog.h"
#include "platform/Paths.hpp"

#include <algorithm>
#include <cstring>
#include <d3d11_4.h>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rxext::genesis_remover {

namespace {

DXGI_FORMAT toDxgi(Format format) {
    switch (format) {
    case Format::R8_UNORM:
        return DXGI_FORMAT_R8_UNORM;
    case Format::R8G8_UNORM:
        return DXGI_FORMAT_R8G8_UNORM;
    case Format::R8G8B8A8_UNORM:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::B8G8R8A8_UNORM:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case Format::R16G16B16A16_SFLOAT:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case Format::None:
        break;
    }
    return DXGI_FORMAT_R8G8B8A8_UNORM;
}

void fillOpaquePassthrough(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::size_t bytes) {
    std::memcpy(out_rgba, in_rgba, bytes);
    for (std::size_t i = 3; i < bytes; i += 4) {
        out_rgba[i] = 255;
    }
}

} // namespace

Input::Input(std::shared_ptr<d3d11::Device> d3d_device, ValueSet settings)
    : d3d_device_(std::move(d3d_device)), settings_(std::move(settings)) {
    removerLog("Input ctor (D3D11)");
}

Input::~Input() {
    async_.stop();
    removerLog("Input dtor");
}

std::unique_ptr<d3d11::Texture> Input::createSharedTexture(ParameterTexture& parameter,
                                                           TextureDesc desc,
                                                           d3d11::Texture::Options options) {
    auto d3d_texture = std::make_unique<d3d11::Texture>(d3d_device_->device(), desc.width, desc.height,
                                                        toDxgi(desc.format), options);
    desc.share_handle = {HandleType::D3D11_IMAGE, d3d_texture->share_handle()};
    parameter.set_texture(host().create_texture(desc));
    return d3d_texture;
}

bool Input::initialize() noexcept try {
    const std::string handle = settings_.get(SettingNames::handle, "main");
    removerLog(std::format("initialize begin handle='{}' ({})", handle, kRemoverBuildStamp));
    host().log_info(std::format("initializing GENESIS Remover stream '{}' ({})", handle,
                                kRemoverBuildStamp));

    const std::filesystem::path module_dir = extensionModuleDirectory();
    const std::filesystem::path roots[] = {module_dir, module_dir / "assets"};
    ::genesis::setAssetSearchRoots(roots);

    sampler_ = add_output_parameter<ParameterTexture>(ParameterNames::sampler);

    time_ = add_parameter<ParameterValue>("time");
    time_->set_property(PropertyNames::purpose, PurposeNames::TimelineTime);

    video_in_param_ = add_parameter<ParameterTexture>("texture");
    video_in_param_->set_property(PropertyNames::name, "Video In");
    video_in_param_->set_property(PropertyNames::group_name, "Input");

    quality_ = add_parameter<ParameterInt>("quality");
    quality_->set_property(PropertyNames::name, "Quality");
    quality_->set_property(PropertyNames::group_name, "Matte");
    quality_->set_property<std::vector<std::string>>(PropertyNames::enum_names,
                                                      {"Fast", "Balanced", "Quality"});
    quality_->set_value(1);

    quad_ = std::make_unique<d3d11::Quad>(d3d_device_->device(), 1.0f);

    if (!update_settings(std::move(settings_))) {
        removerLog("initialize FAILED update_settings");
        return false;
    }
    removerLog("initialize OK (D3D11 shared textures)");
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("initialize exception: {}", ex.what()));
    try {
        host().log_error(std::format("GENESIS Remover init failed: {}", ex.what()));
    } catch (...) {
    }
    return false;
}

bool Input::update_settings(ValueSet settings) noexcept try {
    settings_ = std::move(settings);
    width_ = settings_.get<std::uint32_t>(SettingNames::resolution_x, 1920);
    height_ = settings_.get<std::uint32_t>(SettingNames::resolution_y, 1080);
    removerLog(std::format("update_settings {}x{}", width_, height_));

    TextureDesc desc{};
    desc.width = width_;
    desc.height = height_;
    desc.format = Format::R8G8B8A8_UNORM;
    desc.is_target = true;
    output_tex_ = createSharedTexture(*sampler_, desc, d3d11::Texture::RenderTarget);
    clearOutputTransparent();
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("update_settings exception: {}", ex.what()));
    host().log_error(std::format("GENESIS Remover settings failed: {}", ex.what()));
    return false;
}

void Input::clearOutputTransparent() {
    if (!output_tex_ || !output_tex_->render_target_view()) {
        return;
    }
    const float clear[4] = {0.f, 0.f, 0.f, 0.f};
    d3d_device_->device_context()->ClearRenderTargetView(output_tex_->render_target_view(), clear);
}

void Input::blitPassthrough() {
    if (!output_tex_ || !video_in_tex_ || !quad_) {
        return;
    }
    auto* ctx = d3d_device_->device_context();
    auto* rtv = output_tex_->render_target_view();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    const D3D11_VIEWPORT vp{0.f, 0.f, static_cast<FLOAT>(output_tex_->width()),
                            static_cast<FLOAT>(output_tex_->height()), 0.f, 1.f};
    ctx->RSSetViewports(1, &vp);
    clearOutputTransparent();
    quad_->draw(ctx, video_in_tex_->shader_resource_view());
}

void Input::ensureStaging(std::uint32_t width, std::uint32_t height) {
    if (staging_read_ && staging_write_ && staging_w_ == width && staging_h_ == height) {
        return;
    }
    staging_read_.release();
    staging_write_.release();
    staging_w_ = width;
    staging_h_ = height;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc = {1, 0};
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = 0;
    if (FAILED(d3d_device_->device()->CreateTexture2D(&desc, nullptr, &staging_read_))) {
        throw std::runtime_error("CreateTexture2D staging_read failed");
    }

    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(d3d_device_->device()->CreateTexture2D(&desc, nullptr, &staging_write_))) {
        throw std::runtime_error("CreateTexture2D staging_write failed");
    }
}

bool Input::readVideoInRgba(std::vector<std::uint8_t>& out_rgba) {
    if (!video_in_tex_) {
        return false;
    }
    const auto w = static_cast<std::uint32_t>(video_in_tex_->width());
    const auto h = static_cast<std::uint32_t>(video_in_tex_->height());
    ensureStaging(w, h);
    auto* ctx = d3d_device_->device_context();
    ctx->CopyResource(staging_read_, video_in_tex_->texture());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(staging_read_, 0, D3D11_MAP_READ, 0, &mapped))) {
        return false;
    }
    out_rgba.resize(static_cast<std::size_t>(w) * h * 4u);
    const auto* src = static_cast<const std::uint8_t*>(mapped.pData);
    for (std::uint32_t y = 0; y < h; ++y) {
        std::memcpy(out_rgba.data() + static_cast<std::size_t>(y) * w * 4u,
                    src + static_cast<std::size_t>(y) * mapped.RowPitch,
                    static_cast<std::size_t>(w) * 4u);
    }
    ctx->Unmap(staging_read_, 0);
    return true;
}

bool Input::writeOutputRgba(const std::uint8_t* rgba) {
    if (!output_tex_ || !rgba || !staging_write_) {
        return false;
    }
    const auto w = static_cast<std::uint32_t>(output_tex_->width());
    const auto h = static_cast<std::uint32_t>(output_tex_->height());
    auto* ctx = d3d_device_->device_context();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(staging_write_, 0, D3D11_MAP_WRITE, 0, &mapped))) {
        return false;
    }
    auto* dst = static_cast<std::uint8_t*>(mapped.pData);
    for (std::uint32_t y = 0; y < h; ++y) {
        std::memcpy(dst + static_cast<std::size_t>(y) * mapped.RowPitch,
                    rgba + static_cast<std::size_t>(y) * w * 4u,
                    static_cast<std::size_t>(w) * 4u);
    }
    ctx->Unmap(staging_write_, 0);
    ctx->CopyResource(output_tex_->texture(), staging_write_);
    return true;
}

void Input::applyMatteSettings() {
    matte_settings_.backend = ::genesis::BgRemoverBackend::Auto;
    const int q = quality_ ? std::clamp(quality_->value(), 0, 2) : 1;
    matte_settings_.quality = static_cast<::genesis::BgRemoverQuality>(q);
    if (async_started_) {
        async_.setSettings(matte_settings_);
    }
}

bool Input::update() noexcept try {
    if (!video_in_param_) {
        return true;
    }

    auto allocate = [&](TextureDesc desc) {
        if (desc.width == 0 || desc.height == 0) {
            return;
        }
        if (desc.format == Format::None) {
            desc.format = Format::R8G8B8A8_UNORM;
        }
        desc.is_target = false;
        video_in_tex_ = createSharedTexture(*video_in_param_, desc, d3d11::Texture::Default);
        width_ = static_cast<std::uint32_t>(desc.width);
        height_ = static_cast<std::uint32_t>(desc.height);

        // Keep output the same size as video in.
        TextureDesc out{};
        out.width = desc.width;
        out.height = desc.height;
        out.format = Format::R8G8B8A8_UNORM;
        out.is_target = true;
        output_tex_ = createSharedTexture(*sampler_, out, d3d11::Texture::RenderTarget);

        applyMatteSettings();
        if (!async_started_) {
            async_.start();
            async_started_ = true;
            removerLog("async matte worker started");
        }
        async_.reset(width_, height_);
        ensureStaging(width_, height_);
        removerLog(std::format("video_in D3D11 shared {}x{}", width_, height_));
    };

    update_input_texture(*video_in_param_, allocate);

    // Fallback when format property is missing.
    if (!video_in_tex_) {
        const int prop_w = video_in_param_->get_property<int>(SettingNames::resolution_x, 0);
        const int prop_h = video_in_param_->get_property<int>(SettingNames::resolution_y, 0);
        if (prop_w > 0 && prop_h > 0) {
            TextureDesc desc{};
            desc.width = static_cast<std::size_t>(prop_w);
            desc.height = static_cast<std::size_t>(prop_h);
            desc.format = get_format_by_name(video_in_param_->get_property(SettingNames::format),
                                             Format::R8G8B8A8_UNORM);
            removerLog("video_in allocate via property fallback");
            allocate(desc);
        }
    }
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("update exception: {}", ex.what()));
    host().log_error(std::format("GENESIS Remover update failed: {}", ex.what()));
    return true; // keep stream alive
}

RenderResult Input::render() noexcept try {
    if (!output_tex_) {
        return RenderResult::Succeeded;
    }

    // No Video In yet: transparent clear (safe while scrubbing).
    if (!video_in_tex_) {
        clearOutputTransparent();
        return RenderResult::Succeeded;
    }

    applyMatteSettings();

    // Prefer GPU blit passthrough first so the timeline always shows the feed.
    // Then overlay matte when async has a completed frame.
    bool wrote_matte = false;
    if (async_started_ && readVideoInRgba(scratch_in_)) {
        const auto w = static_cast<std::uint32_t>(video_in_tex_->width());
        const auto h = static_cast<std::uint32_t>(video_in_tex_->height());
        if (scratch_in_.size() >= static_cast<std::size_t>(w) * h * 4u) {
            async_.trySubmit(scratch_in_.data(), w, h);
            scratch_out_.resize(static_cast<std::size_t>(w) * h * 4u);
            if (async_.copyLatest(scratch_out_.data(), w, h)) {
                // Output size may match video; ensure staging matches output.
                if (output_tex_->width() == w && output_tex_->height() == h) {
                    wrote_matte = writeOutputRgba(scratch_out_.data());
                }
            }
        }
    }

    if (!wrote_matte) {
        // ExtSampleD3D-identical path: always safe, shows linked video.
        blitPassthrough();
    }
    return RenderResult::Succeeded;
} catch (const std::exception& ex) {
    removerLog(std::format("render exception: {}", ex.what()));
    try {
        blitPassthrough();
    } catch (...) {
    }
    return RenderResult::Succeeded;
}

ValueSet Input::get_state() noexcept {
    ValueSet state;
    int res_x = static_cast<int>(width_);
    int res_y = static_cast<int>(height_);
    Format format = Format::R8G8B8A8_UNORM;
    if (sampler_) {
        try {
            const auto desc = sampler_->texture().desc();
            if (desc.width > 0 && desc.height > 0) {
                res_x = static_cast<int>(desc.width);
                res_y = static_cast<int>(desc.height);
                format = desc.format;
            }
        } catch (...) {
        }
    }
    state.set(StateNames::resolution_x, res_x);
    state.set(StateNames::resolution_y, res_y);
    state.set(StateNames::format, get_format_name(format));
    state.set(StateNames::scale_y, 1);
    return state;
}

string Input::get_property(string_view name) noexcept {
    if (name == PropertyNames::applies_alpha) {
        return string("true");
    }
    return {};
}

} // namespace rxext::genesis_remover
