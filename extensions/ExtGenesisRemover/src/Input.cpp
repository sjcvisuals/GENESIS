#include "WinPrefix.h"
#include "Input.h"

#include "ModulePaths.h"
#include "RemoverLog.h"
#include "platform/Paths.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace rxext::genesis_remover {

namespace {

// PIXERA host uploads for this extension display as RGBA byte order (cyan must stay cyan).
// Live Video In bytes often arrive B,G,R,A even when the texture is labeled RGBA — that
// caused the brief multi-colour feed. Swizzle on download only; upload stays RGBA.
constexpr Format kPixeraHostFormat = Format::R8G8B8A8_UNORM;

void swapRbInPlace(std::uint8_t* px, std::size_t pixels) {
    for (std::size_t i = 0; i < pixels; ++i) {
        std::swap(px[i * 4u + 0], px[i * 4u + 2]);
    }
}

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
    const std::size_t step = std::max<std::size_t>(1, pixels / 8192u);
    std::size_t hits = 0;
    unsigned max_a = 0;
    for (std::size_t i = 0; i < pixels; i += step) {
        const unsigned a = rgba[i * 4u + 3u];
        max_a = std::max(max_a, a);
        if (a > 8) {
            ++hits;
        }
    }
    // Soft gate: RVM often has sparse person coverage; don't block matte_live forever.
    return max_a > 24 && hits >= 3;
}

void extractAlpha(const std::uint8_t* rgba, std::uint8_t* alpha8, std::size_t pixels) {
    for (std::size_t i = 0; i < pixels; ++i) {
        alpha8[i] = rgba[i * 4u + 3u];
    }
}

void compositeWithAlpha(const std::uint8_t* in_rgba, const std::uint8_t* alpha8,
                        std::uint8_t* out_rgba, std::size_t pixels) {
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::uint8_t a = alpha8[i];
        out_rgba[i * 4u + 0] = static_cast<std::uint8_t>((in_rgba[i * 4u + 0] * a) / 255u);
        out_rgba[i * 4u + 1] = static_cast<std::uint8_t>((in_rgba[i * 4u + 1] * a) / 255u);
        out_rgba[i * 4u + 2] = static_cast<std::uint8_t>((in_rgba[i * 4u + 2] * a) / 255u);
        out_rgba[i * 4u + 3] = a;
    }
}

void fillOpaquePassthrough(const std::uint8_t* in_rgba, std::uint8_t* out_rgba, std::size_t bytes) {
    std::memcpy(out_rgba, in_rgba, bytes);
    for (std::size_t i = 3; i < bytes; i += 4) {
        out_rgba[i] = 255;
    }
}

void fillSolidRgba(std::uint8_t* out_rgba, std::size_t bytes, std::uint8_t r, std::uint8_t g,
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

    // Needed so update() keeps running after a Video In link while the playhead
    // is parked (otherwise we stay on cyan forever).
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
            (cur.width != width_ || cur.height != height_ || cur.format != kPixeraHostFormat);
    } catch (...) {
        need_new = true;
    }
    if (!need_new) {
        return;
    }

    TextureDesc desc{};
    desc.width = width_;
    desc.height = height_;
    desc.format = kPixeraHostFormat;
    desc.is_target = true;
    sampler_->set_texture(host().create_texture(desc));

    // Cyan = DLL alive, waiting for Video In download (unmistakable diagnostic).
    // v10 BGRA upload showed as yellow — PIXERA treats this sampler as RGBA.
    const std::size_t bytes = static_cast<std::size_t>(width_) * height_ * 4u;
    scratch_out_.resize(bytes);
    fillSolidRgba(scratch_out_.data(), bytes, 0, 220, 255);
    BufferDesc buffer{};
    buffer.data = scratch_out_.data();
    buffer.pitch = static_cast<std::size_t>(width_) * 4u;
    buffer.size = bytes;
    host().upload_texture(sampler_->texture(), buffer, true, []() noexcept {});
    matte_live_ = false;
    has_last_alpha_ = false;
    last_alpha_.clear();
    removerLog(std::format("sampler host texture {}x{} format={} (cyan placeholder)", width_,
                           height_, get_format_name(kPixeraHostFormat)));
}

void Input::ensureVideoInHostTexture() {
    if (!video_in_) {
        return;
    }

    auto allocate = [&](TextureDesc desc, const char* reason) {
        if (desc.width == 0 || desc.height == 0) {
            return;
        }
        if (desc.format == Format::None) {
            desc.format = kPixeraHostFormat;
        }
        desc.is_target = false;
        desc.share_handle = {};

        const auto gen = video_in_generation_.fetch_add(1) + 1;
        download_pending_.store(false);

        video_in_->set_texture(host().create_texture(desc));
        video_in_allocated_ = true;
        // Never download in the same step as create — that crashed PIXERA.
        video_in_download_allowed_ = false;
        download_cooldown_frames_ = 8;
        download_pending_ticks_ = 0;
        ensureSampler(static_cast<std::uint32_t>(desc.width),
                      static_cast<std::uint32_t>(desc.height));
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            has_in_ = false;
            latest_in_.clear();
        }
        has_last_alpha_ = false;
        last_alpha_.clear();
        last_render_path_.clear();
        removerLog(std::format("video_in HOST texture {}x{} format={} gen={} ({})", desc.width,
                               desc.height, get_format_name(desc.format), gen, reason));
    };

    update_input_texture(*video_in_, [&](const TextureDesc& desc) {
        allocate(desc, "update_input_texture");
    });

    const int prop_w = video_in_->get_property<int>(SettingNames::resolution_x, 0);
    const int prop_h = video_in_->get_property<int>(SettingNames::resolution_y, 0);
    const std::string format_str(video_in_->get_property(SettingNames::format));
    const Format prop_fmt = get_format_by_name(format_str, kPixeraHostFormat);

    if (!baseline_captured_ && prop_w > 0 && prop_h > 0) {
        baseline_w_ = prop_w;
        baseline_h_ = prop_h;
        baseline_format_ = format_str;
        baseline_captured_ = true;
        removerLog(std::format("video_in baseline props {}x{} format='{}'", prop_w, prop_h,
                               format_str));
    }

    // PIXERA live inputs often omit format, so the SDK helper never fires.
    // Allocate when format is explicit or when props change from the pre-link baseline.
    if (!video_in_allocated_ && prop_w > 0 && prop_h > 0 && baseline_captured_) {
        const bool format_explicit = !format_str.empty();
        const bool dims_changed = (prop_w != baseline_w_ || prop_h != baseline_h_);
        const bool format_changed = (format_str != baseline_format_);
        if (format_explicit || dims_changed || format_changed) {
            TextureDesc desc{};
            desc.width = static_cast<std::size_t>(prop_w);
            desc.height = static_cast<std::size_t>(prop_h);
            desc.format = prop_fmt;
            allocate(desc, format_explicit ? "props+format" : "props-changed");
        } else if (!same_res_allocate_tried_ && pump_ticks_ > 30) {
            // Same-resolution live link: props may never change. Create host texture
            // so PIXERA can write; download arms after cooldown.
            same_res_allocate_tried_ = true;
            TextureDesc desc{};
            desc.width = static_cast<std::size_t>(prop_w);
            desc.height = static_cast<std::size_t>(prop_h);
            desc.format = prop_fmt;
            allocate(desc, "same-res-timeout");
        }
    }

    // Props never populate (common with some live sources) — still allocate at stream size
    // so a later link has a host texture to write into.
    if (!video_in_allocated_ && !settings_fallback_allocate_tried_ && pump_ticks_ > 60) {
        settings_fallback_allocate_tried_ = true;
        TextureDesc desc{};
        desc.width = width_ > 0 ? width_ : 1920;
        desc.height = height_ > 0 ? height_ : 1080;
        desc.format = kPixeraHostFormat;
        allocate(desc, "settings-fallback");
    }

    // Downloads stay empty: recreate once (PIXERA sometimes needs a fresh host texture).
    if (video_in_allocated_ && video_in_download_allowed_ && !empty_realloc_tried_ &&
        downloads_ok_.load() == 0 && downloads_empty_.load() >= 90) {
        empty_realloc_tried_ = true;
        TextureDesc desc{};
        desc.width = width_ > 0 ? width_ : 1920;
        desc.height = height_ > 0 ? height_ : 1080;
        if (prop_w > 0 && prop_h > 0) {
            desc.width = static_cast<std::size_t>(prop_w);
            desc.height = static_cast<std::size_t>(prop_h);
        }
        desc.format = prop_fmt;
        removerLog("video_in realloc after empty downloads");
        allocate(desc, "empty-realloc");
    }

    if (video_in_allocated_ && !video_in_download_allowed_) {
        if (download_cooldown_frames_ > 0) {
            --download_cooldown_frames_;
        } else {
            video_in_download_allowed_ = true;
            removerLog("video_in download armed");
        }
    }

    if (!video_in_allocated_ && (pump_ticks_ % 60u) == 1u) {
        removerLog(std::format("waiting link: props {}x{} format='{}' baseline={} pump={}", prop_w,
                               prop_h, format_str, baseline_captured_ ? 1 : 0, pump_ticks_));
    }
}

void Input::pumpVideoIn() {
    ++pump_ticks_;
    ensureVideoInHostTexture();
    requestVideoDownload();
}

void Input::requestVideoDownload() {
    if (!video_in_ || !video_in_download_allowed_) {
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

    // If PIXERA never invokes the download callback, pending sticks forever.
    if (download_pending_.load()) {
        ++download_pending_ticks_;
        if (download_pending_ticks_ < 120) {
            return;
        }
        download_pending_.store(false);
        download_pending_ticks_ = 0;
        removerLog("download pending stuck — reset");
    }
    if (download_pending_.exchange(true)) {
        return;
    }
    download_pending_ticks_ = 0;

    const auto gen = video_in_generation_.load(std::memory_order_acquire);

    host().download_texture(
        video_in_->texture(),
        [this, gen, w = static_cast<std::uint32_t>(desc.width),
         h = static_cast<std::uint32_t>(desc.height)](BufferDesc buffer,
                                                      OnComplete on_complete) mutable noexcept {
            try {
                if (gen != video_in_generation_.load(std::memory_order_acquire)) {
                    download_pending_.store(false);
                    on_complete();
                    return;
                }
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
                    const std::size_t pixels = static_cast<std::size_t>(w) * h;
                    // Video In arrives as B,G,R,A → swap to R,G,B,A for RVM / display.
                    swapRbInPlace(packed.data(), pixels);
                    const std::size_t lit = nonzeroRgbCount(packed.data(), expected);
                    const std::size_t mid = (pixels / 2u) * 4u;
                    const unsigned pr = packed[mid + 0];
                    const unsigned pg = packed[mid + 1];
                    const unsigned pb = packed[mid + 2];
                    {
                        std::lock_guard<std::mutex> lock(frame_mutex_);
                        latest_in_ = std::move(packed);
                        in_w_ = w;
                        in_h_ = h;
                        has_in_ = true;
                    }
                    const auto n = downloads_ok_.fetch_add(1) + 1;
                    if (n <= 5 || (n % 120) == 0) {
                        removerLog(std::format(
                            "download ok {}x{} swap_rb=1 nonzero_rgb={} mid_rgb=({},{},{}) n={}", w,
                            h, lit, pr, pg, pb, n));
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
    matte_settings_.rvm_downsample = 0.0f; // honor Fast/Balanced/Quality presets
    matte_settings_.temporal_alpha = 0.20f;
    // Resolve model only when the worker is (about to be) running — avoid SDL/asset
    // work during cyan scrub-in before Video In is linked.
    if (async_started_ && matte_settings_.model_path.empty()) {
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
        removerLog(std::format("matte settings applied quality={} ep={}", q,
                               async_.executionProvider()));
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

void Input::uploadSolidDiagnostic(std::uint8_t r, std::uint8_t g, std::uint8_t b, const char* path) {
    if (last_render_path_ == path && (render_ticks_ % 30u) != 0u) {
        return;
    }
    const std::uint32_t w = width_ > 0 ? width_ : 1920;
    const std::uint32_t h = height_ > 0 ? height_ : 1080;
    ensureSampler(w, h);
    const std::size_t bytes = static_cast<std::size_t>(w) * h * 4u;
    scratch_out_.resize(bytes);
    fillSolidRgba(scratch_out_.data(), bytes, r, g, b);
    uploadOutputRgba(scratch_out_.data(), w, h);
    logPathOnce(last_render_path_, path);
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
        "matte status='{}' backend={} ep={} rvm={} infer={}x{} ms={:.1f} submitted={} completed={} "
        "skipped={} busy={} matte_live={} dl_ok={} dl_empty={}",
        status, ::genesis::bgRemoverBackendLabel(async_.activeBackend()),
        async_.executionProvider(), async_.isRvmReady() ? 1 : 0, async_.lastInferWidth(),
        async_.lastInferHeight(), async_.lastInferMs(), async_.submitted(), completed,
        async_.skipped(), async_.busy() ? 1 : 0, matte_live_ ? 1 : 0, downloads_ok_.load(),
        downloads_empty_.load()));
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
    Format format = kPixeraHostFormat;
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
    state.set(StateNames::frame_rate, 50.0);
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
    // Do not call applyMatteSettings / start ORT here — scrub-in must stay light.
    pumpVideoIn();
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("update exception: {}", ex.what()));
    return true;
}

RenderResult Input::render() noexcept try {
    touchHostClock();
    ++render_ticks_;
    // PIXERA may call render without update (or the reverse). Pump both paths.
    pumpVideoIn();
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
        // Stage colours so "just cyan" tells us where we stalled:
        // cyan  = not allocated yet / waiting Video In props
        // blue  = host texture ready, waiting for first download bytes
        // orange = downloads firing but empty (PIXERA not writing)
        if (!video_in_allocated_) {
            uploadSolidDiagnostic(0, 220, 255, "waiting_allocate_cyan");
        } else if (downloads_empty_.load() > 0 && downloads_ok_.load() == 0) {
            uploadSolidDiagnostic(255, 140, 0, "waiting_download_orange");
        } else {
            uploadSolidDiagnostic(30, 90, 220, "waiting_download_blue");
        }
        return RenderResult::Succeeded;
    }

    const std::size_t bytes = static_cast<std::size_t>(w) * h * 4u;
    if (frame.size() < bytes) {
        return RenderResult::Succeeded;
    }

    if (!async_started_) {
        async_.start();
        async_started_ = true;
        matte_settings_applied_ = false; // force push now that worker exists
        applyMatteSettings();
        async_.reset(w, h);
        removerLog(std::format("async matte worker started (after first video frame) ep={}",
                               async_.executionProvider()));
    } else {
        // Live Quality UI changes (setSettings is a no-op when unchanged).
        applyMatteSettings();
    }

    async_.trySubmit(frame.data(), w, h);
    scratch_out_.resize(bytes);
    const std::size_t pixels = static_cast<std::size_t>(w) * h;

    // Prefer a fresh covered matte; otherwise reuse last alpha on the current camera
    // RGB so PIXERA stays smooth when RVM is slower than the host download rate.
    if (async_.copyLatest(scratch_out_.data(), w, h) &&
        matteHasCoverage(scratch_out_.data(), w, h)) {
        last_alpha_.resize(pixels);
        extractAlpha(scratch_out_.data(), last_alpha_.data(), pixels);
        has_last_alpha_ = true;
        if (!matte_live_) {
            matte_live_ = true;
            removerLog(std::format("matte_live=1 ep={} infer={}x{} ({:.1f}ms)",
                                   async_.executionProvider(), async_.lastInferWidth(),
                                   async_.lastInferHeight(), async_.lastInferMs()));
        }
    }

    if (has_last_alpha_ && last_alpha_.size() == pixels) {
        compositeWithAlpha(frame.data(), last_alpha_.data(), scratch_out_.data(), pixels);
        uploadOutputRgba(scratch_out_.data(), w, h);
        logPathOnce(last_render_path_, "matte");
        return RenderResult::Succeeded;
    }

    const std::size_t lit = nonzeroRgbCount(frame.data(), bytes);
    if (lit == 0) {
        // Linked but empty — magenta diagnostic.
        fillSolidRgba(scratch_out_.data(), bytes, 255, 0, 255);
        uploadOutputRgba(scratch_out_.data(), w, h);
        logPathOnce(last_render_path_, "input_black_magenta");
        return RenderResult::Succeeded;
    }

    // Correct-color live feed until RVM coverage is ready (was multi-colour under RGBA host).
    fillOpaquePassthrough(frame.data(), scratch_out_.data(), bytes);
    uploadOutputRgba(scratch_out_.data(), w, h);
    logPathOnce(last_render_path_, "opaque_passthrough");
    return RenderResult::Succeeded;
} catch (const std::exception& ex) {
    removerLog(std::format("render exception: {}", ex.what()));
    return RenderResult::Succeeded;
}

} // namespace rxext::genesis_remover
