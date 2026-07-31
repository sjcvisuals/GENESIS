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

void fillOpaquePassthrough(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::size_t bytes) {
    std::memcpy(out_rgba, in_rgba, bytes);
    for (std::size_t i = 3; i < bytes; i += 4) {
        out_rgba[i] = 255;
    }
}

void fillDebugMagenta(std::uint8_t* out_rgba, std::size_t bytes) {
    for (std::size_t i = 0; i + 3 < bytes; i += 4) {
        out_rgba[i + 0] = 255;
        out_rgba[i + 1] = 0;
        out_rgba[i + 2] = 255;
        out_rgba[i + 3] = 255;
    }
}

} // namespace

Input::Input(std::uint64_t adapter_luid, ValueSet settings)
    : adapter_luid_(adapter_luid), settings_(std::move(settings)) {
    removerLog(std::format("Input ctor adapter_luid={:016x}", adapter_luid_));
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

void Input::ensureSampler(std::uint32_t width, std::uint32_t height) {
    if (!sampler_ || width == 0 || height == 0) {
        return;
    }
    width_ = width;
    height_ = height;

    bool need_new = true;
    try {
        const auto cur = sampler_->texture().desc();
        need_new = (cur.width != width_ || cur.height != height_ ||
                    cur.format != Format::R8G8B8A8_UNORM);
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

    const std::size_t bytes = static_cast<std::size_t>(width_) * height_ * 4u;
    scratch_out_.assign(bytes, 0);
    BufferDesc buffer{};
    buffer.data = scratch_out_.data();
    buffer.pitch = static_cast<std::size_t>(width_) * 4u;
    buffer.size = bytes;
    host().upload_texture(TextureRef(sampler_->texture()), buffer, true, []() noexcept {});
    removerLog(std::format("sampler host texture {}x{}", width_, height_));
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
        // Host-owned texture (ExtSampleShader). PIXERA writes linked media into it.
        desc.is_target = false;
        desc.share_handle = {};
        video_in_->set_texture(host().create_texture(desc));
        ensureSampler(static_cast<std::uint32_t>(desc.width),
                      static_cast<std::uint32_t>(desc.height));
        applyMatteSettings();
        if (!async_started_) {
            async_.start();
            async_started_ = true;
        }
        async_.reset(static_cast<std::uint32_t>(desc.width),
                     static_cast<std::uint32_t>(desc.height));
        removerLog(std::format("video_in HOST texture {}x{} format={}", desc.width, desc.height,
                               get_format_name(desc.format)));
    };

    update_input_texture(*video_in_, allocate);

    // Fallback when format property is missing (common PIXERA case).
    TextureDesc probe{};
    try {
        probe = video_in_->texture().desc();
    } catch (...) {
        probe = {};
    }
    const int prop_w = video_in_->get_property<int>(SettingNames::resolution_x, 0);
    const int prop_h = video_in_->get_property<int>(SettingNames::resolution_y, 0);
    const Format prop_fmt = get_format_by_name(video_in_->get_property(SettingNames::format),
                                               Format::R8G8B8A8_UNORM);

    const bool has_usable =
        probe.width > 0 && probe.height > 0 && probe.format != Format::None;
    if (!has_usable && prop_w > 0 && prop_h > 0) {
        TextureDesc desc{};
        desc.width = static_cast<std::size_t>(prop_w);
        desc.height = static_cast<std::size_t>(prop_h);
        desc.format = prop_fmt;
        removerLog("video_in allocate via property fallback");
        allocate(desc);
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

    host().download_texture(
        TextureRef(video_in_->texture()),
        [this, w = static_cast<std::uint32_t>(desc.width),
         h = static_cast<std::uint32_t>(desc.height)](BufferDesc buffer,
                                                      OnComplete on_complete) mutable noexcept {
            try {
                const std::size_t expected = static_cast<std::size_t>(w) * h * 4u;
                if (!buffer.data || buffer.size < expected || w == 0 || h == 0) {
                    downloads_empty_.fetch_add(1);
                    if (downloads_empty_.load() <= 3) {
                        removerLog(std::format("download empty/short size={} expected={}",
                                               buffer.size, expected));
                    }
                } else {
                    // Respect pitch if larger than tightly packed RGBA.
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
                    downloads_ok_.fetch_add(1);
                    if (downloads_ok_.load() <= 3 || (downloads_ok_.load() % 120) == 0) {
                        removerLog(std::format("download ok {}x{} nonzero_rgb={}", w, h, lit));
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
    matte_settings_.backend = ::genesis::BgRemoverBackend::Auto;
    const int q = quality_ ? std::clamp(quality_->value(), 0, 2) : 1;
    matte_settings_.quality = static_cast<::genesis::BgRemoverQuality>(q);
    if (async_started_) {
        async_.setSettings(matte_settings_);
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
    host().upload_texture(TextureRef(sampler_->texture()), buffer, true, []() noexcept {});
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
    return state;
}

string Input::get_property(string_view name) noexcept {
    if (name == PropertyNames::applies_alpha) {
        return string("true");
    }
    return {};
}

bool Input::update() noexcept try {
    ensureVideoInHostTexture();
    applyMatteSettings();
    requestVideoDownload();
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("update exception: {}", ex.what()));
    return true;
}

SyncDesc Input::before_render() noexcept {
    return {};
}

RenderResult Input::render() noexcept try {
    std::vector<std::uint8_t> frame;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (!has_in_ || latest_in_.empty()) {
            return RenderResult::Succeeded;
        }
        frame = latest_in_;
        w = in_w_;
        h = in_h_;
    }

    const std::size_t bytes = static_cast<std::size_t>(w) * h * 4u;
    if (frame.size() < bytes || w == 0 || h == 0) {
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
    if (!async_.copyLatest(scratch_out_.data(), w, h)) {
        const std::size_t lit = nonzeroRgbCount(frame.data(), bytes);
        if (lit == 0) {
            // Linked but pixels are empty — make failure obvious (magenta).
            fillDebugMagenta(scratch_out_.data(), bytes);
            if (downloads_ok_.load() + downloads_empty_.load() <= 5) {
                removerLog("render: input all black → debug magenta");
            }
        } else {
            fillOpaquePassthrough(frame.data(), scratch_out_.data(), bytes);
        }
    }

    uploadOutputRgba(scratch_out_.data(), w, h);
    return RenderResult::Succeeded;
} catch (const std::exception& ex) {
    removerLog(std::format("render exception: {}", ex.what()));
    return RenderResult::Succeeded;
}

SyncDesc Input::after_render() noexcept {
    return {};
}

} // namespace rxext::genesis_remover
