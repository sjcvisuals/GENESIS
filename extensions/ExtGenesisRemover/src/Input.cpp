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

const char* formatName(Format format) {
    switch (format) {
    case Format::R8_UNORM:
        return "R8";
    case Format::R8G8_UNORM:
        return "RG8";
    case Format::R8G8B8A8_UNORM:
        return "RGBA8";
    case Format::B8G8R8A8_UNORM:
        return "BGRA8";
    case Format::R16G16B16A16_SFLOAT:
        return "RGBA16F";
    case Format::None:
        break;
    }
    return "unknown";
}

bool hasVideoSignal(const std::uint8_t* rgba, std::size_t bytes) {
    if (!rgba || bytes < 4) {
        return false;
    }
    unsigned max_c = 0;
    for (std::size_t i = 0; i + 3 < bytes; i += 64) {
        max_c = std::max(max_c, static_cast<unsigned>(rgba[i + 0]));
        max_c = std::max(max_c, static_cast<unsigned>(rgba[i + 1]));
        max_c = std::max(max_c, static_cast<unsigned>(rgba[i + 2]));
        if (max_c > 12) {
            return true;
        }
    }
    return max_c > 12;
}

bool matteHasCoverage(const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h) {
    if (!rgba || w == 0 || h == 0) {
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(w) * h;
    const std::size_t step = std::max<std::size_t>(1, pixels / 4096u);
    std::size_t hits = 0;
    unsigned max_a = 0;
    for (std::size_t i = 0; i < pixels; i += step) {
        const unsigned a = rgba[i * 4u + 3u];
        max_a = std::max(max_a, a);
        if (a > 16) {
            ++hits;
        }
    }
    return max_a > 32 && hits >= 8;
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

    // Keep live Video In ticking while the playhead sits on the clip.
    frame_counter_ = add_parameter<ParameterValue>("frame_counter");
    frame_counter_->set_property(PropertyNames::purpose, PurposeNames::FrameCounter);

    video_in_param_ = add_parameter<ParameterTexture>("texture");
    video_in_param_->set_property(PropertyNames::name, "Video In");
    video_in_param_->set_property(PropertyNames::group_name, "Input");

    quality_ = add_parameter<ParameterInt>("quality");
    quality_->set_property(PropertyNames::name, "Quality");
    quality_->set_property(PropertyNames::group_name, "Matte");
    quality_->set_property<std::vector<std::string>>(PropertyNames::enum_names,
                                                      {"Fast", "Balanced", "Quality"});
    quality_->set_value(1);

    // Full-frame (1.0). ExtSampleD3D uses 2/3 only as a demo inset.
    quad_ = std::make_unique<d3d11::Quad>(d3d_device_->device(), 1.0f);

    if (!update_settings(std::move(settings_))) {
        removerLog("initialize FAILED update_settings");
        return false;
    }
    removerLog("initialize OK (D3D11 SampleD3D-first)");
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
    // Opaque black until Video In arrives (visible under applies_alpha=false).
    clearOutput(0.f, 0.f, 0.f, 1.f);
    matte_live_ = false;
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("update_settings exception: {}", ex.what()));
    host().log_error(std::format("GENESIS Remover settings failed: {}", ex.what()));
    return false;
}

void Input::clearOutput(float r, float g, float b, float a) {
    if (!output_tex_ || !output_tex_->render_target_view()) {
        return;
    }
    const float clear[4] = {r, g, b, a};
    d3d_device_->device_context()->ClearRenderTargetView(output_tex_->render_target_view(), clear);
}

void Input::blitSampleD3D() {
    if (!output_tex_ || !video_in_tex_ || !quad_) {
        return;
    }
    auto* ctx = d3d_device_->device_context();
    auto* rtv = output_tex_->render_target_view();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    const D3D11_VIEWPORT vp{0.f, 0.f, static_cast<FLOAT>(output_tex_->width()),
                            static_cast<FLOAT>(output_tex_->height()), 0.f, 1.f};
    ctx->RSSetViewports(1, &vp);
    // Same pattern as ExtSampleD3D Input::render — GPU sample after fence wait.
    clearOutput(0.f, 0.f, 0.f, 1.f);
    quad_->draw(ctx, video_in_tex_->shader_resource_view());
}

void Input::ensureStaging(std::uint32_t width, std::uint32_t height, DXGI_FORMAT format) {
    if (staging_read_ && staging_write_ && staging_w_ == width && staging_h_ == height &&
        staging_format_ == format) {
        return;
    }
    staging_read_.release();
    staging_write_.release();
    staging_w_ = width;
    staging_h_ = height;
    staging_format_ = format;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc = {1, 0};
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = 0;
    if (FAILED(d3d_device_->device()->CreateTexture2D(&desc, nullptr, &staging_read_))) {
        throw std::runtime_error("CreateTexture2D staging_read failed");
    }

    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
    const DXGI_FORMAT src_format = video_in_tex_->format();
    if (src_format != DXGI_FORMAT_R8G8B8A8_UNORM && src_format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        return false;
    }

    ensureStaging(w, h, src_format);
    auto* ctx = d3d_device_->device_context();
    ctx->CopyResource(staging_read_, video_in_tex_->texture());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(staging_read_, 0, D3D11_MAP_READ, 0, &mapped))) {
        return false;
    }
    out_rgba.resize(static_cast<std::size_t>(w) * h * 4u);
    const auto* src = static_cast<const std::uint8_t*>(mapped.pData);
    const bool bgra = (src_format == DXGI_FORMAT_B8G8R8A8_UNORM);
    for (std::uint32_t y = 0; y < h; ++y) {
        const auto* row = src + static_cast<std::size_t>(y) * mapped.RowPitch;
        auto* dst = out_rgba.data() + static_cast<std::size_t>(y) * w * 4u;
        if (!bgra) {
            std::memcpy(dst, row, static_cast<std::size_t>(w) * 4u);
            continue;
        }
        for (std::uint32_t x = 0; x < w; ++x) {
            const auto* p = row + static_cast<std::size_t>(x) * 4u;
            dst[x * 4u + 0] = p[2];
            dst[x * 4u + 1] = p[1];
            dst[x * 4u + 2] = p[0];
            dst[x * 4u + 3] = p[3];
        }
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
    const int q = quality_ ? std::clamp(quality_->value(), 0, 2) : 1;
    const auto next_q = static_cast<::genesis::BgRemoverQuality>(q);

    if (matte_settings_applied_ && matte_settings_.backend == ::genesis::BgRemoverBackend::Auto &&
        matte_settings_.quality == next_q) {
        return;
    }

    matte_settings_.backend = ::genesis::BgRemoverBackend::Auto;
    matte_settings_.quality = next_q;
    if (matte_settings_.model_path.empty()) {
        try {
            matte_settings_.model_path =
                ::genesis::resolveAssetPath("assets/matting/rvm_mobilenetv3_fp16.onnx");
            removerLog(std::format("RVM model: {}", matte_settings_.model_path.string()));
        } catch (const std::exception& ex) {
            removerLog(std::format("RVM model resolve failed: {}", ex.what()));
        }
    }

    matte_settings_applied_ = true;
    if (async_started_) {
        async_.setSettings(matte_settings_);
        removerLog(std::format("matte settings applied quality={} backend=Auto", q));
    }
}

void Input::touchHostClock() {
    if (time_) {
        (void)time_->value();
    }
    if (frame_counter_) {
        (void)frame_counter_->value();
    }
}

void Input::logMatteStatusMaybe() {
    if (!async_started_) {
        return;
    }
    const auto completed = async_.completed();
    const std::string status = async_.status();
    const bool status_changed = status != last_matte_status_;
    const bool completed_changed = completed != last_logged_completed_;
    if (!status_changed && !completed_changed && (render_ticks_ % 120u) != 0u) {
        return;
    }
    last_matte_status_ = status;
    last_logged_completed_ = completed;
    removerLog(std::format(
        "matte status='{}' backend={} rvm={} submitted={} completed={} skipped={} busy={} "
        "matte_live={}",
        status, ::genesis::bgRemoverBackendLabel(async_.activeBackend()),
        async_.isRvmReady() ? 1 : 0, async_.submitted(), completed, async_.skipped(),
        async_.busy() ? 1 : 0, matte_live_ ? 1 : 0));
}

void Input::logRenderPathOnce(const char* path) {
    if (last_render_path_ == path) {
        return;
    }
    last_render_path_ = path;
    removerLog(std::format("render path={} video={}x{} fmt={} applies_alpha={}", path, width_,
                           height_, formatName(video_in_format_), matte_live_ ? "true" : "false"));
}

bool Input::update() noexcept try {
    touchHostClock();
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
        video_in_format_ = desc.format;
        video_in_tex_ = createSharedTexture(*video_in_param_, desc, d3d11::Texture::Default);
        width_ = static_cast<std::uint32_t>(desc.width);
        height_ = static_cast<std::uint32_t>(desc.height);

        TextureDesc out{};
        out.width = desc.width;
        out.height = desc.height;
        out.format = Format::R8G8B8A8_UNORM;
        out.is_target = true;
        output_tex_ = createSharedTexture(*sampler_, out, d3d11::Texture::RenderTarget);
        matte_live_ = false;

        if (!async_started_) {
            async_.start();
            async_started_ = true;
            removerLog("async matte worker started");
        }
        applyMatteSettings();
        async_.reset(width_, height_);
        ensureStaging(width_, height_, toDxgi(video_in_format_));
        last_render_path_.clear();
        removerLog(std::format("video_in D3D11 shared {}x{} fmt={}", width_, height_,
                               formatName(video_in_format_)));
    };

    update_input_texture(*video_in_param_, allocate);

    if (!video_in_tex_) {
        TextureDesc desc{};
        desc.width = static_cast<std::size_t>(
            video_in_param_->get_property<int>(SettingNames::resolution_x, 0));
        desc.height = static_cast<std::size_t>(
            video_in_param_->get_property<int>(SettingNames::resolution_y, 0));
        desc.format = get_format_by_name(video_in_param_->get_property(SettingNames::format),
                                         Format::R8G8B8A8_UNORM);

        if (desc.width == 0 || desc.height == 0) {
            try {
                const auto cur = video_in_param_->texture().desc();
                if (cur.width > 0 && cur.height > 0) {
                    desc.width = cur.width;
                    desc.height = cur.height;
                    if (cur.format != Format::None) {
                        desc.format = cur.format;
                    }
                    removerLog("video_in allocate via texture().desc() fallback");
                }
            } catch (...) {
            }
        } else {
            removerLog("video_in allocate via property fallback");
        }

        if (desc.width > 0 && desc.height > 0) {
            allocate(desc);
        }
    }
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("update exception: {}", ex.what()));
    host().log_error(std::format("GENESIS Remover update failed: {}", ex.what()));
    return true;
}

RenderResult Input::render() noexcept try {
    touchHostClock();
    ++render_ticks_;

    if (!output_tex_) {
        return RenderResult::Succeeded;
    }

    if (!video_in_tex_) {
        clearOutput(0.f, 0.f, 0.f, 1.f);
        logRenderPathOnce("no_video_in");
        return RenderResult::Succeeded;
    }

    applyMatteSettings();
    logMatteStatusMaybe();

    // ------------------------------------------------------------------
    // Phase A (always): ExtSampleD3D GPU blit.
    // With applies_alpha=false until matte_live_, webcam A=0 still shows RGB.
    // Never skip this for "empty CPU signal" — that latch caused permanent black.
    // ------------------------------------------------------------------
    blitSampleD3D();
    logRenderPathOnce("sample_d3d_blit");

    // ------------------------------------------------------------------
    // Phase B (optional): overlay async matte when it has real coverage.
    // ------------------------------------------------------------------
    const auto w = static_cast<std::uint32_t>(video_in_tex_->width());
    const auto h = static_cast<std::uint32_t>(video_in_tex_->height());
    if (!async_started_ || output_tex_->width() != w || output_tex_->height() != h) {
        return RenderResult::Succeeded;
    }
    if (!readVideoInRgba(scratch_in_)) {
        return RenderResult::Succeeded;
    }
    const auto bytes = static_cast<std::size_t>(w) * h * 4u;
    if (scratch_in_.size() < bytes || !hasVideoSignal(scratch_in_.data(), bytes)) {
        return RenderResult::Succeeded;
    }

    async_.trySubmit(scratch_in_.data(), w, h);
    scratch_out_.resize(bytes);
    if (!async_.copyLatest(scratch_out_.data(), w, h) ||
        !matteHasCoverage(scratch_out_.data(), w, h)) {
        return RenderResult::Succeeded;
    }
    if (!writeOutputRgba(scratch_out_.data())) {
        return RenderResult::Succeeded;
    }
    if (!matte_live_) {
        matte_live_ = true;
        removerLog("matte_live=1 (applies_alpha enabled for cutout)");
    }
    logRenderPathOnce("matte");
    return RenderResult::Succeeded;
} catch (const std::exception& ex) {
    removerLog(std::format("render exception: {}", ex.what()));
    try {
        blitSampleD3D();
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
    state.set(StateNames::frame_rate, 60.0);
    return state;
}

string Input::get_property(string_view name) noexcept {
    if (name == PropertyNames::applies_alpha) {
        // ExtSampleD3D never sets this. Returning true while webcam A=0 made the
        // whole layer invisible. Enable only after a covered matte is on screen.
        return string(matte_live_ ? "true" : "false");
    }
    return {};
}

} // namespace rxext::genesis_remover
