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
    if (!d3d_matte_src_) {
        d3d_matte_src_ = std::make_unique<D3d11SharedTexture>(d3d_->device(), d3d_->context());
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
        // Drop the old share so PIXERA cannot keep sampling a stale first frame.
        d3d_video_in_ = std::make_unique<D3d11SharedTexture>(d3d_->device(), d3d_->context());
    }
    if (d3d_matte_src_) {
        d3d_matte_src_ = std::make_unique<D3d11SharedTexture>(d3d_->device(), d3d_->context());
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
        // Private matte RT: never shared — Map this, not the output PIXERA displays.
        d3d_matte_src_->allocate(width_, height_, Format::R8G8B8A8_UNORM, /*is_target=*/true,
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
    const std::string ui = std::format("v34 {} nz={} mid=({},{},{}) vin={}",
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
        debug_status_->set_value(std::string("v34 private Map - link Live Input"));
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
        // Clear only — never uploadRgba8/CopyResource onto shared output (v33 freeze).
        if (d3d_output_ && d3d_output_->hasTexture()) {
            d3d_output_->clear(0.0f, 0.86f, 1.0f, 1.0f); // cyan
            if (d3d_ && d3d_->context()) {
                d3d_->context()->Flush();
            }
        }
        logStatusMaybe(dbg);
        return RenderResult::Succeeded;
    }

    // Heartbeat: first frames after VIDEO_IN must appear or PIXERA hung on share open.
    if (render_ticks_ <= 5u || (render_ticks_ % 30u) == 1u) {
        removerLog(std::format("render vin-ready enter ticks={} fence={}/{}", render_ticks_,
                               d3d_ ? d3d_->completedValue() : 0,
                               d3d_ ? d3d_->fenceCounter() : 0));
    }

    // Device::render already queued GPU Wait on fenceCounter (ExtSampleD3D). Do NOT
    // waitCpu here — a timeout that skips blit leaves the previous face on screen.
    // Live blit always runs; CPU Map uses waitGpuIdle on our private RT only.

    // Live path: blit Video In → shared output (what PIXERA shows).
    // Matte path: blit Video In → private RGBA8 RT, wait GPU, Map private only.
    // Never Map/CopyResource Video In or shared output (v32/v33: zeros + frozen frame).
    if (!passthrough_ || !passthrough_->ready() || !d3d_video_in_->srv() || !d3d_output_ ||
        !d3d_output_->rtv() || !d3d_matte_src_ || !d3d_matte_src_->rtv()) {
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

    // Ensure private matte RT matches current size (allocate is no-op if unchanged).
    d3d_matte_src_->allocate(width_, height_, Format::R8G8B8A8_UNORM, /*is_target=*/true,
                             /*publish_share=*/false);

    // Always refresh shared output from Video In (same as ExtSampleD3D Quad) — this is
    // the live preview path and must not depend on Map/CopyResource.
    passthrough_->draw(d3d_->context(), d3d_video_in_->srv(), d3d_output_->rtv(), width_, height_);
    passthrough_->draw(d3d_->context(), d3d_video_in_->srv(), d3d_matte_src_->rtv(), width_,
                       height_);
    if (!d3d_->waitGpuIdle(80)) {
        if (render_ticks_ <= 5u || (render_ticks_ % 60u) == 1u) {
            removerLog("waitGpuIdle after blit timed out — keep live blit, skip Map");
        }
        dbg.width = width_;
        dbg.height = height_;
        dbg.has_video_in = true;
        dbg.download_ok = false;
        dbg.stage = "passthrough"; // shared output blit still submitted
        logStatusMaybe(dbg);
        return RenderResult::Succeeded;
    }
    if (render_ticks_ <= 8u || (render_ticks_ % 60u) == 1u) {
        removerLog(std::format("gpu_blit×2 Video In → output+private {}x{} fmt={}", width_, height_,
                               get_format_name(d3d_video_in_->format())));
    }

    long map_hr = 0;
    if (!d3d_matte_src_->downloadTargetRgba8(d3d_rgba_in_, &map_hr)) {
        dbg.width = width_;
        dbg.height = height_;
        dbg.has_video_in = true;
        dbg.download_ok = false;
        dbg.stage = "download_fail";
        if (render_ticks_ <= 5u || (render_ticks_ % 30u) == 1u) {
            removerLog(std::format("private matte Map failed hr=0x{:08x}",
                                   static_cast<unsigned long>(map_hr)));
        }
        logStatusMaybe(dbg);
        return RenderResult::Succeeded;
    }

    if ((render_ticks_ % 30u) == 1u) {
        D3d11SharedTexture::logFrameStats(d3d_rgba_in_, width_, height_, "private_after_blit");
    }

    // Detect frozen Video In (PIXERA wrote once then stopped): same mid for many frames.
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
            if (any && same_mid_frames_ == 90u) {
                removerLog(std::format(
                    "frozen_suspect same mid=({},{},{},{}) for 90 frames — Video In may not be "
                    "updating",
                    mid[0], mid[1], mid[2], mid[3]));
            }
        }
    }

    if (!remover_->asyncEnabled()) {
        applyMatteSettings();
        remover_->setAsyncEnabled(true);
    }

    (void)remover_->processCpuRgbaFrame(d3d_rgba_in_.data(), true, &error, &dbg, true,
                                        &d3d_rgba_out_);

    // Never CopyResource onto shared output (v33 freeze). Upload matte into private RT,
    // then GPU-blit private → shared (ExtSampleD3D-safe).
    if (dbg.stage && std::string_view(dbg.stage) == "matte" && dbg.nonzero_rgb > 0 &&
        d3d_matte_src_ && d3d_matte_src_->srv() && d3d_output_ && d3d_output_->rtv() &&
        !d3d_rgba_out_.empty()) {
        if (!d3d_matte_src_->uploadRgba8(d3d_rgba_out_.data(), width_, height_)) {
            removerLog("private matte uploadRgba8 failed");
        } else {
            passthrough_->draw(d3d_->context(), d3d_matte_src_->srv(), d3d_output_->rtv(), width_,
                               height_);
            matte_live_ = true;
            if ((render_ticks_ % 60u) == 1u) {
                removerLog("matte_ok blit private → shared output");
            }
        }
    } else if (dbg.nonzero_rgb == 0) {
        // Video In empty this frame — force shared clear so preview cannot stick on last face.
        if (d3d_output_ && d3d_output_->hasTexture()) {
            d3d_output_->clear(0.0f, 0.0f, 0.0f, 1.0f);
        }
        matte_live_ = false;
        if ((render_ticks_ % 30u) == 1u) {
            removerLog("private_after_blit nz=0 — cleared shared output (no stale face)");
        }
    } else if (dbg.nonzero_rgb > 0 && (render_ticks_ % 60u) == 1u) {
        removerLog(std::format("private_after_blit nz={} stage={} (live blit on shared output)",
                               dbg.nonzero_rgb, dbg.stage ? dbg.stage : "?"));
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
