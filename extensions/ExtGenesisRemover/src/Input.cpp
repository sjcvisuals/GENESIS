#include "WinPrefix.h"
#include "Input.h"

#include "ModulePaths.h"
#include "RemoverLog.h"
#include "common/string.h"
#include "matting/BgRemover.hpp"
#include "platform/Paths.hpp"
#include "rxext_util.h"

#include <Windows.h>
#include <d3d11.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

namespace rxext::genesis_remover {

namespace {

int stageToEnum(const char* stage) {
    if (!stage) {
        return 0;
    }
    const std::string_view s(stage);
    if (s == "waiting_video_in") {
        return 1;
    }
    if (s == "download_fail") {
        return 2;
    }
    if (s == "input_black") {
        return 3;
    }
    if (s == "passthrough") {
        return 4;
    }
    if (s == "matte") {
        return 5;
    }
    if (s == "error") {
        return 6;
    }
    return 0;
}
} // namespace

Input::Input(std::shared_ptr<SharedD3d11> d3d, std::uint64_t adapter_luid, ValueSet settings)
    : d3d_(std::move(d3d)), adapter_luid_(adapter_luid), settings_(std::move(settings)) {
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

    debug_stage_ = add_parameter<ParameterInt>("debug_stage");
    debug_stage_->set_property(PropertyNames::name, "Debug Stage");
    debug_stage_->set_property(PropertyNames::group_name, "Debug");
    debug_stage_->set_property<std::vector<std::string>>(
        PropertyNames::enum_names,
        {"none", "waiting_video_in (cyan)", "download_fail (blue)", "input_black (orange)",
         "passthrough", "matte", "error"});
    debug_stage_->set_value(0);

    debug_status_ = add_parameter<ParameterString>("debug_status");
    debug_status_->set_property(PropertyNames::name, "Debug Status");
    debug_status_->set_property(PropertyNames::group_name, "Debug");
    debug_status_->set_value(std::string("booting"));

    removerLog(std::format("Input ctor full-D3D11 I/O adapter_luid={:016x}", adapter_luid_));
}

Input::~Input() {
    removerLog("Input dtor");
}

void Input::ensureRemover() {
    if (remover_) {
        return;
    }
    // Vulkan context still used internally for matte scratch; PIXERA only sees D3D11_IMAGE.
    remover_ = std::make_unique<::genesis::RemoverPipeline>(adapter_luid_);
    removerLog(std::format("RemoverPipeline ready adapter={:016x}", adapter_luid_));
}

void Input::ensureD3dTextures() {
    if (!d3d_) {
        throw std::runtime_error("Input: SharedD3d11 is null");
    }
    if (!d3d_output_) {
        d3d_output_ = std::make_unique<D3d11SharedTexture>(d3d_->device(), d3d_->context());
    }
    if (!d3d_video_in_) {
        d3d_video_in_ = std::make_unique<D3d11SharedTexture>(d3d_->device(), d3d_->context());
    }
    if (!d3d_capture_) {
        d3d_capture_ = std::make_unique<D3d11SharedTexture>(d3d_->device(), d3d_->context());
    }
    if (!d3d_matte_tex_) {
        d3d_matte_tex_ = std::make_unique<D3d11SharedTexture>(d3d_->device(), d3d_->context());
    }
    if (!passthrough_) {
        passthrough_ = std::make_unique<D3d11Passthrough>(d3d_->device());
    }
}

void Input::clearVideoInLink(const char* reason) {
    if (!video_in_ready_ && (!d3d_video_in_ || !d3d_video_in_->hasTexture())) {
        return;
    }
    removerLog(std::format("video_in cleared ({})", reason ? reason : "?"));
    video_in_ready_ = false;
    matte_live_ = false;
    same_mid_frames_ = 0;
    last_mid_rgba_ = {};
    if (d3d_video_in_) {
        d3d_video_in_ = std::make_unique<D3d11SharedTexture>(d3d_->device(), d3d_->context());
    }
    if (d3d_capture_) {
        d3d_capture_ = std::make_unique<D3d11SharedTexture>(d3d_->device(), d3d_->context());
    }
    if (d3d_matte_tex_) {
        d3d_matte_tex_ = std::make_unique<D3d11SharedTexture>(d3d_->device(), d3d_->context());
    }
    if (video_in_) {
        try {
            video_in_->set_texture({});
        } catch (...) {
        }
    }
    if (d3d_output_ && d3d_output_->hasTexture()) {
        d3d_output_->clear(0.0f, 0.86f, 1.0f, 1.0f); // cyan
        if (d3d_ && d3d_->context()) {
            d3d_->context()->Flush();
        }
    }
}

void Input::applyMatteSettings() {
    if (!remover_) {
        return;
    }
    ::genesis::BgRemoverSettings matte = remover_->remover().settings();
    const int q = quality_ ? std::clamp(quality_->value(), 0, 2) : 1;
    const auto next_q = static_cast<::genesis::BgRemoverQuality>(q);
    if (matte.backend == ::genesis::BgRemoverBackend::Auto && matte.quality == next_q) {
        return;
    }
    matte.backend = ::genesis::BgRemoverBackend::Auto;
    matte.quality = next_q;
    matte.rvm_downsample = 0.0f;
    matte.temporal_alpha = 0.20f;
    remover_->setSettings(matte);
}

void Input::publishTargetTexture() {
    if (!sampler_ || !d3d_) {
        return;
    }
    ensureD3dTextures();
    d3d_output_->allocate(width_, height_, Format::R8G8B8A8_UNORM, /*is_target=*/true);

    TextureDesc desc{};
    desc.width = width_;
    desc.height = height_;
    desc.format = Format::R8G8B8A8_UNORM;
    desc.is_target = true;
    // Match ExtSampleD3D: type + handle only (process_id = 0).
    desc.share_handle = {HandleType::D3D11_IMAGE,
                         reinterpret_cast<Handle>(d3d_output_->shareHandle())};
    sampler_->set_texture(host().create_texture(desc));
    removerLog(std::format("shared OUTPUT {}x{} handle=D3D11_IMAGE", desc.width, desc.height));
}

void Input::publishVideoInTexture() {
    if (!video_in_ || !remover_) {
        return;
    }

    auto allocate = [&](const TextureDesc& desc) {
        if (desc.width == 0 || desc.height == 0) {
            return;
        }
        width_ = static_cast<std::uint32_t>(desc.width);
        height_ = static_cast<std::uint32_t>(desc.height);
        const Format fmt = desc.format == Format::None ? Format::R8G8B8A8_UNORM : desc.format;
        removerLog(std::format("video_in allocate {}x{} format={} (D3D11_IMAGE)", width_, height_,
                               get_format_name(fmt)));

        ensureD3dTextures();
        // ExtSampleD3D Video In: SRV only (is_target=false).
        d3d_video_in_->allocate(width_, height_, fmt, /*is_target=*/false);
        d3d_capture_->allocate(width_, height_, Format::R8G8B8A8_UNORM, /*is_target=*/true,
                               /*publish_share=*/false);
        d3d_matte_tex_->allocate(width_, height_, Format::R8G8B8A8_UNORM, /*is_target=*/true,
                                 /*publish_share=*/false);
        remover_->resize(width_, height_);
        publishTargetTexture();

        TextureDesc shared = desc;
        shared.format = fmt;
        shared.is_target = false;
        shared.share_handle = {HandleType::D3D11_IMAGE,
                               reinterpret_cast<Handle>(d3d_video_in_->shareHandle())};
        video_in_->set_texture(host().create_texture(shared));
        video_in_ready_ = true;
        applyMatteSettings();
        removerLog(std::format(
            "shared VIDEO_IN {}x{} format={} handle=D3D11_IMAGE is_target=0 srv={} (link feed now)",
            shared.width, shared.height, get_format_name(shared.format),
            d3d_video_in_->srv() ? 1 : 0));
        matte_live_ = false;
    };

    update_input_texture(*video_in_, allocate);

    const int prop_w = video_in_->get_property<int>(SettingNames::resolution_x, 0);
    const int prop_h = video_in_->get_property<int>(SettingNames::resolution_y, 0);

    // Unlink: props cleared after we had a Video In — drop stale first-frame texture.
    if (video_in_ready_ && prop_w <= 0 && prop_h <= 0) {
        clearVideoInLink("props cleared");
        return;
    }

    if (!video_in_ready_) {
        TextureDesc desc{};
        desc.width = static_cast<std::size_t>(prop_w);
        desc.height = static_cast<std::size_t>(prop_h);
        desc.format = get_format_by_name(video_in_->get_property(SettingNames::format),
                                         Format::R8G8B8A8_UNORM);
        if (desc.width == 0 || desc.height == 0) {
            try {
                const auto cur = video_in_->texture().desc();
                if (cur.width > 0 && cur.height > 0) {
                    desc.width = cur.width;
                    desc.height = cur.height;
                    if (cur.format != Format::None) {
                        desc.format = cur.format;
                    }
                }
            } catch (...) {
            }
        }
        if (desc.width > 0 && desc.height > 0) {
            removerLog("video_in allocate via fallback (D3D11)");
            allocate(desc);
        } else if ((render_ticks_ % 60u) == 1u) {
            removerLog(std::format(
                "waiting Video In link (props {}x{} format='{}') - cyan is correct until then",
                prop_w, prop_h, std::string(video_in_->get_property(SettingNames::format))));
        }
    }
}

void Input::logStatusMaybe(const ::genesis::RemoverFrameDebug& dbg) {
    if (debug_stage_) {
        debug_stage_->set_value(stageToEnum(dbg.stage));
    }
    const std::string ui = std::format("v37 {} nz={} mid=({},{},{}) vin={}",
                                       dbg.stage ? dbg.stage : "?", dbg.nonzero_rgb, dbg.mid_r,
                                       dbg.mid_g, dbg.mid_b, dbg.has_video_in ? 1 : 0);
    if (debug_status_) {
        debug_status_->set_value(ui);
    }
    if (last_stage_ != (dbg.stage ? dbg.stage : "") || (render_ticks_ % 30u) == 1u) {
        last_stage_ = dbg.stage ? dbg.stage : "";
        const std::uint64_t fence_c = d3d_ ? d3d_->fenceCounter() : 0;
        const std::uint64_t fence_done = d3d_ ? d3d_->completedValue() : 0;
        const std::uint64_t sync_n = d3d_ ? d3d_->syncCallbacks() : 0;
        const int sent =
            (!d3d_rgba_in_.empty() && D3d11SharedTexture::looksLikeSentinel(d3d_rgba_in_)) ? 1 : 0;
        removerLog(std::format(
            "STATUS stage={} vin={} dl={} nz={} mid=({},{},{},{}) {}x{} matte={} luid={:016x} "
            "fence={}/{} sync={} sent={}",
            dbg.stage ? dbg.stage : "?", dbg.has_video_in ? 1 : 0, dbg.download_ok ? 1 : 0,
            dbg.nonzero_rgb, dbg.mid_r, dbg.mid_g, dbg.mid_b, dbg.mid_a, dbg.width, dbg.height,
            dbg.matte_copied ? 1 : 0, adapter_luid_, fence_done, fence_c, sync_n, sent));
    }
}

bool Input::initialize() noexcept try {
    const std::string handle = settings_.get(SettingNames::handle, "main");
    removerLog(std::format("initialize begin handle='{}' adapter_luid={:016x} ({})", handle,
                           adapter_luid_, kRemoverBuildStamp));
    host().log_info(std::format("initializing GENESIS Remover stream '{}' ({})", handle,
                                kRemoverBuildStamp));
    return update_settings(std::move(settings_));
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
    if (const string stream_luid = settings_.get(SettingNames::adapter_luid, string{});
        !stream_luid.empty()) {
        adapter_luid_ = common::from_hex_string<std::uint64_t>(stream_luid);
    }
    removerLog(std::format("update_settings {}x{} adapter={:016x}", width_, height_,
                           adapter_luid_));

    ensureRemover();
    remover_->resize(width_, height_);
    publishTargetTexture();
    if (debug_status_) {
        debug_status_->set_value(std::string("v37 live+async matte - link Live Input"));
    }
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("update_settings exception: {}", ex.what()));
    try {
        host().log_error(std::format("GENESIS Remover settings failed: {}", ex.what()));
    } catch (...) {
    }
    return false;
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
        // false until matte — transparent black looks like "nothing" in preview.
        return string(matte_live_ ? "true" : "false");
    }
    return {};
}

bool Input::update() noexcept try {
    if (time_) {
        (void)time_->value();
    }
    if (frame_counter_) {
        (void)frame_counter_->value();
    }
    ensureRemover();
    publishVideoInTexture();
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("update exception: {}", ex.what()));
    return true;
}

SyncDesc Input::before_render() noexcept {
    return {};
}

void Input::tryFinishAsyncMatte(::genesis::RemoverFrameDebug& dbg, std::string& error) {
    if (!d3d_capture_ || !d3d_capture_->asyncDownloadPending()) {
        return;
    }
    long map_hr = 0;
    const int poll = d3d_capture_->pollAsyncDownload(d3d_rgba_in_, &map_hr);
    if (poll == 0) {
        return; // still on GPU - do not wait
    }
    if (poll < 0) {
        if ((render_ticks_ % 60u) == 1u) {
            removerLog(std::format("async capture poll failed hr=0x{:08x}",
                                   static_cast<unsigned long>(map_hr)));
        }
        return;
    }

    if ((render_ticks_ % 30u) == 1u) {
        D3d11SharedTexture::logFrameStats(d3d_rgba_in_, width_, height_, "async_capture");
    }

    bool any_px = false;
    for (std::uint8_t b : d3d_rgba_in_) {
        if (b) {
            any_px = true;
            break;
        }
    }
    if (!any_px) {
        dbg.width = width_;
        dbg.height = height_;
        dbg.has_video_in = true;
        dbg.download_ok = true;
        dbg.nonzero_rgb = 0;
        dbg.stage = "input_black";
        if ((render_ticks_ % 60u) == 1u) {
            removerLog("async_capture nz=0 (keep live blit)");
        }
        return;
    }

    if (d3d_rgba_in_.size() >= 4 && width_ > 0 && height_ > 0) {
        const std::size_t mid_i =
            (static_cast<std::size_t>(height_ / 2u) * width_ + (width_ / 2u)) * 4u;
        if (mid_i + 3 < d3d_rgba_in_.size()) {
            const std::array<std::uint8_t, 4> mid = {
                d3d_rgba_in_[mid_i], d3d_rgba_in_[mid_i + 1], d3d_rgba_in_[mid_i + 2],
                d3d_rgba_in_[mid_i + 3]};
            const bool any = (mid[0] | mid[1] | mid[2]) != 0;
            if (any && mid == last_mid_rgba_) {
                ++same_mid_frames_;
            } else {
                same_mid_frames_ = 0;
                last_mid_rgba_ = mid;
            }
            if (any && same_mid_frames_ == 45u) {
                removerLog(std::format(
                    "frozen_suspect same mid=({},{},{},{}) for 45 captures", mid[0], mid[1], mid[2],
                    mid[3]));
            }
        }
    }

    if (!remover_->asyncEnabled()) {
        applyMatteSettings();
        remover_->setAsyncEnabled(true);
    }

    (void)remover_->processCpuRgbaFrame(d3d_rgba_in_.data(), true, &error, &dbg, false,
                                        &d3d_rgba_out_);

    if (dbg.stage && std::string_view(dbg.stage) == "matte" && dbg.nonzero_rgb > 0 &&
        d3d_matte_tex_ && !d3d_rgba_out_.empty()) {
        d3d_matte_tex_->allocate(width_, height_, Format::R8G8B8A8_UNORM, /*is_target=*/true,
                                 /*publish_share=*/false);
        if (d3d_matte_tex_->uploadRgba8(d3d_rgba_out_.data(), width_, height_)) {
            matte_live_ = true;
            if ((render_ticks_ % 60u) == 1u) {
                removerLog("matte_ok async uploaded to private matte RT");
            }
        } else if ((render_ticks_ % 60u) == 1u) {
            removerLog("matte uploadRgba8 to private failed");
        }
    } else if (dbg.nonzero_rgb > 0 && (render_ticks_ % 60u) == 1u) {
        removerLog(std::format("async_capture nz={} stage={}", dbg.nonzero_rgb,
                               dbg.stage ? dbg.stage : "?"));
    }
}

void Input::maybeStartAsyncCapture() {
    if (!d3d_capture_ || !passthrough_ || !d3d_video_in_ || !d3d_video_in_->srv()) {
        return;
    }
    if (d3d_capture_->asyncDownloadPending()) {
        return;
    }
    // Capture every other frame - leaves GPU time for live blit + fence signal.
    if ((render_ticks_ % 2u) != 0u) {
        return;
    }
    d3d_capture_->allocate(width_, height_, Format::R8G8B8A8_UNORM, /*is_target=*/true,
                           /*publish_share=*/false);
    if (!d3d_capture_->rtv()) {
        return;
    }
    passthrough_->draw(d3d_->context(), d3d_video_in_->srv(), d3d_capture_->rtv(), width_, height_);
    if (!d3d_capture_->beginAsyncDownload()) {
        if ((render_ticks_ % 120u) == 1u) {
            removerLog("beginAsyncDownload failed");
        }
    }
}

RenderResult Input::render() noexcept try {
    ++render_ticks_;
    if (time_) {
        (void)time_->value();
    }
    if (frame_counter_) {
        (void)frame_counter_->value();
    }

    ensureRemover();
    ensureD3dTextures();

    ::genesis::RemoverFrameDebug dbg{};
    std::string error;

    if (!video_in_ready_ || !d3d_video_in_ || !d3d_video_in_->hasTexture()) {
        (void)remover_->processCpuRgbaFrame(nullptr, false, &error, &dbg, true, &d3d_rgba_out_);
        if (d3d_output_ && d3d_output_->hasTexture()) {
            d3d_output_->clear(0.0f, 0.86f, 1.0f, 1.0f); // cyan
        }
        logStatusMaybe(dbg);
        return RenderResult::Succeeded;
    }

    if (render_ticks_ <= 5u || (render_ticks_ % 30u) == 1u) {
        removerLog(std::format("render vin-ready enter ticks={} fence={}/{}", render_ticks_,
                               d3d_ ? d3d_->completedValue() : 0,
                               d3d_ ? d3d_->fenceCounter() : 0));
    }

    if (!passthrough_ || !passthrough_->ready() || !d3d_video_in_->srv() || !d3d_output_ ||
        !d3d_output_->rtv()) {
        dbg.width = width_;
        dbg.height = height_;
        dbg.has_video_in = true;
        dbg.download_ok = false;
        dbg.stage = "download_fail";
        if (d3d_output_ && d3d_output_->hasTexture()) {
            d3d_output_->clear(0.12f, 0.35f, 0.86f, 1.0f);
        }
        logStatusMaybe(dbg);
        return RenderResult::Succeeded;
    }

    // 1) Finish any completed async capture (never blocks).
    tryFinishAsyncMatte(dbg, error);

    // 2) Present: prefer last matte cutout; else live Video In (v36-proven path).
    if (matte_live_ && d3d_matte_tex_ && d3d_matte_tex_->srv()) {
        passthrough_->draw(d3d_->context(), d3d_matte_tex_->srv(), d3d_output_->rtv(), width_,
                           height_);
        if (!dbg.stage || std::string_view(dbg.stage) == "none" ||
            std::string_view(dbg.stage) == "passthrough") {
            dbg.stage = "matte";
            dbg.has_video_in = true;
            dbg.download_ok = true;
            dbg.matte_copied = true;
            dbg.width = width_;
            dbg.height = height_;
        }
    } else {
        d3d_output_->clear(0.0f, 0.0f, 0.0f, 1.0f);
        passthrough_->draw(d3d_->context(), d3d_video_in_->srv(), d3d_output_->rtv(), width_,
                           height_);
        dbg.width = width_;
        dbg.height = height_;
        dbg.has_video_in = true;
        if (!dbg.stage || std::string_view(dbg.stage) == "none") {
            dbg.stage = "passthrough";
        }
    }

    // 3) Kick next capture (CopyResource+End query only - no Map/wait).
    maybeStartAsyncCapture();

    if (render_ticks_ <= 8u || (render_ticks_ % 60u) == 1u) {
        removerLog(std::format(
            "gpu_present {} {}x{} capture_pending={} matte_live={}",
            matte_live_ ? "matte" : "live", width_, height_,
            (d3d_capture_ && d3d_capture_->asyncDownloadPending()) ? 1 : 0, matte_live_ ? 1 : 0));
    }

    if (!error.empty() && (render_ticks_ % 60u) == 1u) {
        removerLog(std::format("processCpuRgbaFrame: {}", error));
    }
    logStatusMaybe(dbg);
    return RenderResult::Succeeded;
} catch (const std::exception& ex) {
    removerLog(std::format("render exception: {}", ex.what()));
    return RenderResult::Succeeded;
}

SyncDesc Input::after_render() noexcept {
    return {};
}

} // namespace rxext::genesis_remover
