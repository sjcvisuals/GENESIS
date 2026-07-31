#include "WinPrefix.h"
#include "Input.h"

#include "ModulePaths.h"
#include "RemoverLog.h"
#include "platform/Paths.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <vector>

namespace rxext::genesis_remover {

namespace {

std::size_t nonzeroRgbCount(const std::uint8_t* rgba, std::size_t bytes) {
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < bytes; i += 4) {
        if (rgba[i] | rgba[i + 1] | rgba[i + 2]) {
            ++n;
        }
    }
    return n;
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

void fillOpaquePassthrough(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::size_t bytes) {
    std::memcpy(out_rgba, in_rgba, bytes);
    for (std::size_t i = 3; i < bytes; i += 4) {
        out_rgba[i] = 255;
    }
}

void fillSolid(std::uint8_t* out_rgba, std::size_t bytes, std::uint8_t r, std::uint8_t g,
               std::uint8_t b) {
    for (std::size_t i = 0; i + 3 < bytes; i += 4) {
        out_rgba[i + 0] = r;
        out_rgba[i + 1] = g;
        out_rgba[i + 2] = b;
        out_rgba[i + 3] = 255;
    }
}

void logPathOnce(std::string& last, const char* path) {
    if (last == path) {
        return;
    }
    last = path;
    removerLog(std::format("render path={}", path));
}

} // namespace

Input::Input(ValueSet settings) : settings_(std::move(settings)) {
    removerLog("Input ctor (host-texture)");
}

Input::~Input() {
    async_.stop();
    removerLog("Input dtor");
}

void Input::registerParameters() {
    if (params_registered_) {
        return;
    }
    const std::filesystem::path module_dir = extensionModuleDirectory();
    const std::filesystem::path roots[] = {module_dir, module_dir / "assets"};
    ::genesis::setAssetSearchRoots(roots);

    sampler_ = add_output_parameter<ParameterTexture>(ParameterNames::sampler);

    time_ = add_parameter<ParameterValue>("time");
    time_->set_property(PropertyNames::purpose, PurposeNames::TimelineTime);

    frame_counter_ = add_parameter<ParameterValue>("frame_counter");
    frame_counter_->set_property(PropertyNames::purpose, PurposeNames::FrameCounter);

    video_in_ = add_parameter<ParameterTexture>("texture");
    video_in_->set_property(PropertyNames::name, "Video In");
    video_in_->set_property(PropertyNames::group_name, "Input");

    quality_ = add_parameter<ParameterInt>("quality");
    quality_->set_property(PropertyNames::name, "Quality");
    quality_->set_property(PropertyNames::group_name, "Matte");
    quality_->set_property<std::vector<std::string>>(PropertyNames::enum_names,
                                                      {"Fast", "Balanced", "Quality"});
    quality_->set_value(1);

    params_registered_ = true;
    removerLog("parameters registered (host-texture path)");
}

void Input::touchHostClock() {
    if (time_) {
        (void)time_->value();
    }
    if (frame_counter_) {
        (void)frame_counter_->value();
    }
}

void Input::ensureSampler(std::uint32_t width, std::uint32_t height) {
    if (!sampler_ || width == 0 || height == 0) {
        return;
    }
    width_ = width;
    height_ = height;

    bool need_new = true;
    try {
        const auto cur = sampler_->texture().desc();
        need_new =
            (cur.width != width_ || cur.height != height_ || cur.format != Format::R8G8B8A8_UNORM);
    } catch (...) {
        need_new = true;
    }
    if (!need_new) {
        return;
    }

    TextureDesc desc{};
    desc.width = width_;
    desc.height = height_;
    desc.format = Format::R8G8B8A8_UNORM;
    desc.is_target = true;
    sampler_->set_texture(host().create_texture(desc));

    // Cyan = DLL alive, waiting for Video In download (unmistakable diagnostic).
    const std::size_t bytes = static_cast<std::size_t>(width_) * height_ * 4u;
    scratch_out_.resize(bytes);
    fillSolid(scratch_out_.data(), bytes, 0, 220, 255);
    BufferDesc buffer{};
    buffer.data = scratch_out_.data();
    buffer.pitch = static_cast<std::size_t>(width_) * 4u;
    buffer.size = bytes;
    host().upload_texture(sampler_->texture(), buffer, true, []() noexcept {});
    matte_live_ = false;
    removerLog(std::format("sampler host texture {}x{} (cyan placeholder)", width_, height_));
}

void Input::ensureVideoInHostTexture() {
    if (!video_in_) {
        return;
    }

    auto allocate = [&](TextureDesc desc) {
        if (desc.width == 0 || desc.height == 0) {
            return;
        }
        if (desc.format == Format::None) {
            desc.format = Format::R8G8B8A8_UNORM;
        }
        // Host-owned texture (ExtSampleShader). No D3D11/Vulkan share handle.
        desc.is_target = false;
        desc.share_handle = {};
        video_in_->set_texture(host().create_texture(desc));
        ensureSampler(static_cast<std::uint32_t>(desc.width),
                      static_cast<std::uint32_t>(desc.height));
        if (!async_started_) {
            async_.start();
            async_started_ = true;
            removerLog("async matte worker started");
        }
        applyMatteSettings();
        async_.reset(static_cast<std::uint32_t>(desc.width),
                     static_cast<std::uint32_t>(desc.height));
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            has_in_ = false;
            latest_in_.clear();
        }
        last_render_path_.clear();
        removerLog(std::format("video_in HOST texture {}x{} format={}", desc.width, desc.height,
                               get_format_name(desc.format)));
    };

    update_input_texture(*video_in_, allocate);

    TextureDesc probe{};
    try {
        probe = video_in_->texture().desc();
    } catch (...) {
        probe = {};
    }
    const int prop_w = video_in_->get_property<int>(SettingNames::resolution_x, 0);
    const int prop_h = video_in_->get_property<int>(SettingNames::resolution_y, 0);
    const Format prop_fmt =
        get_format_by_name(video_in_->get_property(SettingNames::format), Format::R8G8B8A8_UNORM);

    const bool has_usable = probe.width > 0 && probe.height > 0 && probe.format != Format::None;
    if (!has_usable && prop_w > 0 && prop_h > 0) {
        TextureDesc desc{};
        desc.width = static_cast<std::size_t>(prop_w);
        desc.height = static_cast<std::size_t>(prop_h);
        desc.format = prop_fmt;
        removerLog("video_in allocate via property fallback");
        allocate(desc);
    } else if (!has_usable) {
        try {
            const auto cur = video_in_->texture().desc();
            if (cur.width > 0 && cur.height > 0) {
                TextureDesc desc = cur;
                if (desc.format == Format::None) {
                    desc.format = Format::R8G8B8A8_UNORM;
                }
                removerLog("video_in allocate via texture().desc() fallback");
                allocate(desc);
            }
        } catch (...) {
        }
    }
}

void Input::requestVideoDownload() {
    if (!video_in_) {
        return;
    }
    TextureDesc desc{};
    try {
        desc = video_in_->texture().desc();
    } catch (...) {
        return;
    }
    if (desc.width == 0 || desc.height == 0 || !video_in_->texture()) {
        return;
    }
    if (download_pending_.exchange(true)) {
        return;
    }

    // Copy TextureRef (acquire); HostContext::download_texture release()s that copy.
    host().download_texture(
        video_in_->texture(),
        [this, w = static_cast<std::uint32_t>(desc.width),
         h = static_cast<std::uint32_t>(desc.height)](BufferDesc buffer,
                                                      OnComplete on_complete) mutable noexcept {
            try {
                const std::size_t expected = static_cast<std::size_t>(w) * h * 4u;
                if (!buffer.data || buffer.size < expected || w == 0 || h == 0) {
                    const auto n = downloads_empty_.fetch_add(1) + 1;
                    if (n <= 5 || (n % 120) == 0) {
                        removerLog(std::format("download empty/short size={} expected={} n={}",
                                               buffer.size, expected, n));
                    }
                } else {
                    std::vector<std::uint8_t> packed(expected);
                    const auto* src = static_cast<const std::uint8_t*>(buffer.data);
                    const std::size_t src_pitch =
                        buffer.pitch > 0 ? buffer.pitch : static_cast<std::size_t>(w) * 4u;
                    for (std::uint32_t y = 0; y < h; ++y) {
                        std::memcpy(packed.data() + static_cast<std::size_t>(y) * w * 4u,
                                    src + static_cast<std::size_t>(y) * src_pitch,
                                    static_cast<std::size_t>(w) * 4u);
                    }
                    const std::size_t lit = nonzeroRgbCount(packed.data(), expected);
                    {
                        std::lock_guard<std::mutex> lock(frame_mutex_);
                        latest_in_ = std::move(packed);
                        in_w_ = w;
                        in_h_ = h;
                        has_in_ = true;
                    }
                    const auto n = downloads_ok_.fetch_add(1) + 1;
                    if (n <= 5 || (n % 120) == 0) {
                        removerLog(std::format("download ok {}x{} nonzero_rgb={} n={}", w, h, lit, n));
                    }
                }
            } catch (...) {
                removerLog("download callback exception");
            }
            download_pending_.store(false);
            on_complete();
        });
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
        removerLog(std::format("matte settings applied quality={}", q));
    }
}

void Input::uploadOutputRgba(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height) {
    if (!sampler_ || !rgba || width == 0 || height == 0) {
        return;
    }
    ensureSampler(width, height);
    const std::size_t bytes = static_cast<std::size_t>(width) * height * 4u;
    BufferDesc buffer{};
    buffer.data = rgba;
    buffer.pitch = static_cast<std::size_t>(width) * 4u;
    buffer.size = bytes;
    host().upload_texture(sampler_->texture(), buffer, true, []() noexcept {});
}

void Input::logMatteStatusMaybe() {
    if (!async_started_) {
        return;
    }
    const auto completed = async_.completed();
    const std::string status = async_.status();
    if (status == last_matte_status_ && completed == last_logged_completed_ &&
        (render_ticks_ % 120u) != 0u) {
        return;
    }
    last_matte_status_ = status;
    last_logged_completed_ = completed;
    removerLog(std::format(
        "matte status='{}' backend={} rvm={} submitted={} completed={} skipped={} busy={} "
        "matte_live={} dl_ok={} dl_empty={}",
        status, ::genesis::bgRemoverBackendLabel(async_.activeBackend()),
        async_.isRvmReady() ? 1 : 0, async_.submitted(), completed, async_.skipped(),
        async_.busy() ? 1 : 0, matte_live_ ? 1 : 0, downloads_ok_.load(), downloads_empty_.load()));
}

bool Input::initialize() noexcept try {
    const std::string handle = settings_.get(SettingNames::handle, "main");
    removerLog(std::format("initialize begin handle='{}' ({})", handle, kRemoverBuildStamp));
    host().log_info(std::format("initializing GENESIS Remover stream '{}' ({})", handle,
                                kRemoverBuildStamp));

    registerParameters();
    width_ = settings_.get<std::uint32_t>(SettingNames::resolution_x, 1920);
    height_ = settings_.get<std::uint32_t>(SettingNames::resolution_y, 1080);
    ensureSampler(width_, height_);
    removerLog("initialize OK (host-texture path)");
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
    if (!params_registered_) {
        registerParameters();
    }
    ensureSampler(width_, height_);
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("update_settings exception: {}", ex.what()));
    return true;
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
        // false until covered matte is live — live webcam A=0 + applies_alpha=true = black.
        return string(matte_live_ ? "true" : "false");
    }
    return {};
}

bool Input::update() noexcept try {
    touchHostClock();
    ensureVideoInHostTexture();
    applyMatteSettings();
    requestVideoDownload();
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("update exception: {}", ex.what()));
    return true;
}

RenderResult Input::render() noexcept try {
    touchHostClock();
    ++render_ticks_;
    logMatteStatusMaybe();

    std::vector<std::uint8_t> frame;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (has_in_ && !latest_in_.empty()) {
            frame = latest_in_;
            w = in_w_;
            h = in_h_;
        }
    }

    if (frame.empty() || w == 0 || h == 0) {
        // Keep cyan placeholder visible so we know the DLL is rendering.
        logPathOnce(last_render_path_, "waiting_download_cyan");
        return RenderResult::Succeeded;
    }

    const std::size_t bytes = static_cast<std::size_t>(w) * h * 4u;
    if (frame.size() < bytes) {
        return RenderResult::Succeeded;
    }

    if (!async_started_) {
        async_.start();
        async_started_ = true;
        applyMatteSettings();
        async_.reset(w, h);
    }

    async_.trySubmit(frame.data(), w, h);
    scratch_out_.resize(bytes);

    if (async_.copyLatest(scratch_out_.data(), w, h) && matteHasCoverage(scratch_out_.data(), w, h)) {
        uploadOutputRgba(scratch_out_.data(), w, h);
        if (!matte_live_) {
            matte_live_ = true;
            removerLog("matte_live=1 (applies_alpha enabled for cutout)");
        }
        logPathOnce(last_render_path_, "matte");
        return RenderResult::Succeeded;
    }

    const std::size_t lit = nonzeroRgbCount(frame.data(), bytes);
    if (lit == 0) {
        // Linked but empty — magenta diagnostic.
        fillSolid(scratch_out_.data(), bytes, 255, 0, 255);
        uploadOutputRgba(scratch_out_.data(), w, h);
        logPathOnce(last_render_path_, "input_black_magenta");
        return RenderResult::Succeeded;
    }

    fillOpaquePassthrough(frame.data(), scratch_out_.data(), bytes);
    uploadOutputRgba(scratch_out_.data(), w, h);
    logPathOnce(last_render_path_, "opaque_passthrough");
    return RenderResult::Succeeded;
} catch (const std::exception& ex) {
    removerLog(std::format("render exception: {}", ex.what()));
    return RenderResult::Succeeded;
}

} // namespace rxext::genesis_remover
