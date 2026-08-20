#include "WinPrefix.h"
#include "Input.h"

#include "ModulePaths.h"
#include "RemoverLog.h"
#include "common/string.h"
#include "platform/Paths.hpp"
#include "rxext_util.h"

#include <Windows.h>
#include <d3d11.h>

#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
    quality_->set_value(0);

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
    debug_status_->set_value(std::string("v45 live + fast oop matte host"));

    removerLog(std::format("Input ctor v45 oop-fast adapter_luid={:016x}", adapter_luid_));
}

Input::~Input() {
    matte_host_.reset();
    removerLog("Input dtor");
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
    if (!d3d_capture_share_) {
        d3d_capture_share_ = std::make_unique<D3d11SharedTexture>(d3d_->device(), d3d_->context());
    }
    if (!d3d_matte_tex_) {
        d3d_matte_tex_ = std::make_unique<D3d11SharedTexture>(d3d_->device(), d3d_->context());
    }
    if (!passthrough_) {
        passthrough_ = std::make_unique<D3d11Passthrough>(d3d_->device());
    }
}

void Input::ensureMatteHost() {
    if (!matte_host_) {
        matte_host_ = std::make_unique<MatteHostClient>();
    }
    if (quality_) {
        matte_host_->setQuality(quality_->value());
    }
    (void)matte_host_->ensureStarted(adapter_luid_);
}

void Input::clearVideoInLink(const char* reason) {
    if (!video_in_ready_ && (!d3d_video_in_ || !d3d_video_in_->hasTexture())) {
        return;
    }
    removerLog(std::format("video_in cleared ({})", reason ? reason : "?"));
    video_in_ready_ = false;
    matte_live_ = false;
    if (d3d_video_in_) {
        d3d_video_in_ = std::make_unique<D3d11SharedTexture>(d3d_->device(), d3d_->context());
    }
    d3d_capture_share_ = std::make_unique<D3d11SharedTexture>(d3d_->device(), d3d_->context());
    last_capture_kick_tick_ = 0;
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
        d3d_output_->clear(0.0f, 0.86f, 1.0f, 1.0f);
    }
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
    desc.share_handle = {HandleType::D3D11_IMAGE,
                         reinterpret_cast<Handle>(d3d_output_->shareHandle())};
    sampler_->set_texture(host().create_texture(desc));
    removerLog(std::format("shared OUTPUT {}x{} handle=D3D11_IMAGE", desc.width, desc.height));
}

void Input::publishVideoInTexture() {
    if (!video_in_) {
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
        ensureMatteHost();
        d3d_video_in_->allocate(width_, height_, fmt, /*is_target=*/false);
        d3d_capture_share_->allocate(width_, height_, Format::R8G8B8A8_UNORM, /*is_target=*/true,
                                     /*publish_share=*/true);
        d3d_matte_tex_->allocate(width_, height_, Format::R8G8B8A8_UNORM, /*is_target=*/true,
                                 /*publish_share=*/false);
        publishTargetTexture();

        TextureDesc shared = desc;
        shared.format = fmt;
        shared.is_target = false;
        shared.share_handle = {HandleType::D3D11_IMAGE,
                               reinterpret_cast<Handle>(d3d_video_in_->shareHandle())};
        video_in_->set_texture(host().create_texture(shared));
        video_in_ready_ = true;
        matte_live_ = false;
        last_capture_kick_tick_ = 0;
        removerLog(std::format(
            "shared VIDEO_IN {}x{} format={} handle=D3D11_IMAGE is_target=0 srv={} (oop matte)",
            shared.width, shared.height, get_format_name(shared.format),
            d3d_video_in_->srv() ? 1 : 0));
        removerLog("capture share ready — matte via GenesisRemoverMatteHost.exe");
    };

    update_input_texture(*video_in_, allocate);

    const int prop_w = video_in_->get_property<int>(SettingNames::resolution_x, 0);
    const int prop_h = video_in_->get_property<int>(SettingNames::resolution_y, 0);

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
    const bool host_ready = matte_host_ && matte_host_->hostReady();
    const std::string ui =
        std::format("v45 {} vin={} host={} matte={}", dbg.stage ? dbg.stage : "?",
                    dbg.has_video_in ? 1 : 0, host_ready ? 1 : 0, matte_live_ ? 1 : 0);
    if (debug_status_) {
        debug_status_->set_value(ui);
    }
    if (last_stage_ != (dbg.stage ? dbg.stage : "") || (render_ticks_ % 30u) == 1u) {
        last_stage_ = dbg.stage ? dbg.stage : "";
        const std::uint64_t fence_c = d3d_ ? d3d_->fenceCounter() : 0;
        const std::uint64_t fence_done = d3d_ ? d3d_->completedValue() : 0;
        const std::uint64_t sync_n = d3d_ ? d3d_->syncCallbacks() : 0;
        removerLog(std::format(
            "STATUS stage={} vin={} dl={} nz={} mid=({},{},{},{}) {}x{} matte={} host={} "
            "luid={:016x} fence={}/{} sync={}",
            dbg.stage ? dbg.stage : "?", dbg.has_video_in ? 1 : 0, dbg.download_ok ? 1 : 0,
            dbg.nonzero_rgb, dbg.mid_r, dbg.mid_g, dbg.mid_b, dbg.mid_a, dbg.width, dbg.height,
            dbg.matte_copied ? 1 : 0, host_ready ? 1 : 0, adapter_luid_, fence_done, fence_c,
            sync_n));
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
    removerLog(std::format("update_settings {}x{} adapter={:016x} (oop matte host)", width_,
                           height_, adapter_luid_));

    ensureMatteHost();
    publishTargetTexture();
    if (debug_status_) {
        debug_status_->set_value(std::string("v45 live + fast GenesisRemoverMatteHost"));
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

    ensureD3dTextures();
    ensureMatteHost();

    ::genesis::RemoverFrameDebug dbg{};

    if (!video_in_ready_ || !d3d_video_in_ || !d3d_video_in_->hasTexture()) {
        dbg.width = width_;
        dbg.height = height_;
        dbg.stage = "waiting_video_in";
        if (d3d_output_ && d3d_output_->hasTexture()) {
            d3d_output_->clear(0.0f, 0.86f, 1.0f, 1.0f);
        }
        logStatusMaybe(dbg);
        return RenderResult::Succeeded;
    }

    if (render_ticks_ <= 5u || (render_ticks_ % 30u) == 1u) {
        removerLog(std::format("render vin-ready enter ticks={} fence={}/{} host={}",
                               render_ticks_, d3d_ ? d3d_->completedValue() : 0,
                               d3d_ ? d3d_->fenceCounter() : 0,
                               matte_host_ && matte_host_->hostReady() ? 1 : 0));
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

    if (quality_ && matte_host_) {
        matte_host_->setQuality(quality_->value());
    }

    std::uint32_t mw = 0;
    std::uint32_t mh = 0;
    ::genesis::RemoverFrameDebug host_dbg{};
    if (matte_host_->tryTakeMatte(host_matte_rgba_, mw, mh, &host_dbg) && mw == width_ &&
        mh == height_ && !host_matte_rgba_.empty()) {
        d3d_matte_tex_->allocate(width_, height_, Format::R8G8B8A8_UNORM, /*is_target=*/true,
                                 /*publish_share=*/false);
        if (d3d_matte_tex_->uploadRgba8UpdateSubresource(host_matte_rgba_.data(), width_,
                                                         height_)) {
            matte_live_ = true;
            dbg = host_dbg;
            removerLog(std::format("applied host matte nz={}", dbg.nonzero_rgb));
        }
    }

    if (matte_live_ && d3d_matte_tex_ && d3d_matte_tex_->srv()) {
        passthrough_->draw(d3d_->context(), d3d_matte_tex_->srv(), d3d_output_->rtv(), width_,
                           height_);
        dbg.width = width_;
        dbg.height = height_;
        dbg.has_video_in = true;
        dbg.download_ok = true;
        dbg.matte_copied = true;
        dbg.stage = "matte";
    } else {
        d3d_output_->clear(0.0f, 0.0f, 0.0f, 1.0f);
        passthrough_->draw(d3d_->context(), d3d_video_in_->srv(), d3d_output_->rtv(), width_,
                           height_);
        passthrough_->drawHeartbeat(d3d_->context(), d3d_output_->rtv(), width_, height_,
                                    render_ticks_);
        dbg.width = width_;
        dbg.height = height_;
        dbg.has_video_in = true;
        dbg.stage = "passthrough";
    }

    // Kick host as soon as idle (was ~2s interval — looked like low FPS).
    constexpr std::uint64_t kFirstKickTick = 30;
    constexpr std::uint64_t kKickInterval = 2;
    if (matte_host_ && matte_host_->hostReady() && !matte_host_->busy() &&
        render_ticks_ >= kFirstKickTick &&
        (last_capture_kick_tick_ == 0 ||
         render_ticks_ - last_capture_kick_tick_ >= kKickInterval) &&
        d3d_capture_share_ && d3d_capture_share_->rtv() && width_ <= 1920 && height_ <= 1080) {
        if (d3d_capture_share_->width() != width_ || d3d_capture_share_->height() != height_) {
            d3d_capture_share_->allocate(width_, height_, Format::R8G8B8A8_UNORM,
                                        /*is_target=*/true, /*publish_share=*/true);
        }
        passthrough_->draw(d3d_->context(), d3d_video_in_->srv(), d3d_capture_share_->rtv(),
                           width_, height_);
        if (matte_host_->kickCapture(*d3d_capture_share_, width_, height_)) {
            last_capture_kick_tick_ = render_ticks_;
        }
    } else if (matte_host_ && !matte_host_->hostReady() && (render_ticks_ % 60u) == 1u) {
        removerLog("waiting GenesisRemoverMatteHost.exe ready (ORT loads out-of-process)");
    }

    if (render_ticks_ <= 8u || (render_ticks_ % 60u) == 1u) {
        removerLog(std::format("gpu_present {} {}x{} matte_live={} host_ready={} busy={}",
                               matte_live_ ? "matte" : "live", width_, height_,
                               matte_live_ ? 1 : 0, matte_host_ && matte_host_->hostReady() ? 1 : 0,
                               matte_host_ && matte_host_->busy() ? 1 : 0));
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
