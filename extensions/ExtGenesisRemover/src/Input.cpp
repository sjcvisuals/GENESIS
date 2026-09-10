#include "WinPrefix.h"
#include "Input.h"

#include "ModulePaths.h"
#include "RemoverLog.h"
#include "common/string.h"
#include "platform/Paths.hpp"
#include "rxext_util.h"

#include <Windows.h>
#include <d3d11.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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
                                                      {"Fast", "Balanced", "Quality", "Custom"});
    quality_->set_value(0);

    custom_downsample_ = add_parameter<ParameterValue>("custom_downsample", 0.25);
    custom_downsample_->set_property(PropertyNames::name, "Custom Downsample");
    custom_downsample_->set_property(PropertyNames::group_name, "Matte");
    custom_downsample_->set_property(PropertyNames::min_value, 0.08);
    custom_downsample_->set_property(PropertyNames::max_value, 1.0);
    custom_downsample_->set_property(PropertyNames::min_value_ui, 0.08);
    custom_downsample_->set_property(PropertyNames::max_value_ui, 1.0);

    custom_infer_side_ = add_parameter<ParameterInt>("custom_infer_side", 512);
    custom_infer_side_->set_property(PropertyNames::name, "Custom Infer Side");
    custom_infer_side_->set_property(PropertyNames::group_name, "Matte");
    custom_infer_side_->set_property(PropertyNames::min_value, 192);
    custom_infer_side_->set_property(PropertyNames::max_value, 1280);

    camera_delay_ms_ = add_parameter<ParameterValue>("camera_delay_ms", 0.0);
    camera_delay_ms_->set_property(PropertyNames::name, "Camera Delay (ms)");
    camera_delay_ms_->set_property(PropertyNames::group_name, "Sync");
    camera_delay_ms_->set_property(PropertyNames::min_value, 0.0);
    camera_delay_ms_->set_property(PropertyNames::max_value, 300.0);
    camera_delay_ms_->set_property(PropertyNames::min_value_ui, 0.0);
    camera_delay_ms_->set_property(PropertyNames::max_value_ui, 300.0);

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
    debug_status_->set_value(std::string("v56 no-fence + update kick (ExtOnnxVision)"));

    removerLog(std::format("Input ctor v56 ExtOnnxVision-style adapter_luid={:016x}", adapter_luid_));
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

void Input::pushQualityToHost() {
    if (!matte_host_ || !quality_) {
        return;
    }
    const int q = quality_->value();
    float ds = 0.0f;
    std::uint32_t side = 0;
    if (q >= 3) {
        ds = custom_downsample_ ? static_cast<float>(custom_downsample_->value()) : 0.25f;
        side = custom_infer_side_ ? static_cast<std::uint32_t>(custom_infer_side_->value()) : 512u;
    }
    if (q == pushed_quality_ && ds == pushed_ds_ && side == pushed_side_) {
        return;
    }
    pushed_quality_ = q;
    pushed_ds_ = ds;
    pushed_side_ = side;
    matte_host_->setQuality(q, ds, side);
}

void Input::ensureMatteHost() {
    if (!matte_host_) {
        matte_host_ = std::make_unique<MatteHostClient>();
    }
    pushQualityToHost();
    (void)matte_host_->ensureStarted(adapter_luid_);
}

void Input::clearVideoInLink(const char* reason) {
    if (!video_in_ready_ && (!d3d_video_in_ || !d3d_video_in_->hasTexture())) {
        return;
    }
    removerLog(std::format("video_in cleared ({})", reason ? reason : "?"));
    video_in_ready_ = false;
    matte_live_ = false;
    live_delay_.reset();
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
        std::format("v56 {} vin={} host={} matte={} ep={}", dbg.stage ? dbg.stage : "?",
                    dbg.has_video_in ? 1 : 0, host_ready ? 1 : 0, matte_live_ ? 1 : 0,
                    matte_host_ ? matte_host_->hostEp() : "?");
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
            "ep={} luid={:016x} fence={}/{} sync={}",
            dbg.stage ? dbg.stage : "?", dbg.has_video_in ? 1 : 0, dbg.download_ok ? 1 : 0,
            dbg.nonzero_rgb, dbg.mid_r, dbg.mid_g, dbg.mid_b, dbg.mid_a, dbg.width, dbg.height,
            dbg.matte_copied ? 1 : 0, host_ready ? 1 : 0,
            matte_host_ ? matte_host_->hostEp() : "?", adapter_luid_, fence_done, fence_c,
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
        debug_status_->set_value(std::string("v56 no-fence + update kick (ExtOnnxVision)"));
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
    // ExtOnnxVision: drive matte off update(), skip when busy — never stall Device::render.
    ensureD3dTextures();
    ensureMatteHost();
    if (quality_ && matte_host_) {
        pushQualityToHost();
    }
    kickMatteIfReady();
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("update exception: {}", ex.what()));
    return true;
}

void Input::kickMatteIfReady() {
    if (!matte_host_ || !video_in_ready_ || !d3d_video_in_ || !d3d_video_in_->srv() ||
        !passthrough_ || !passthrough_->ready() || !d3d_capture_share_ || width_ < 16 ||
        height_ < 16) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    // Shared-capture Map stalls the PIXERA GPU. When map_ms spikes, stop kicking.
    constexpr double kMinKickMs = 50.0;
    constexpr double kStressedKickMs = 100.0;
    constexpr float kSlowMapMs = 45.0f;
    constexpr std::uint32_t kSlowMapStreak = 2;
    constexpr double kCooldownMs = 2000.0;

    matte_host_->clearBusyIfStale(2500);
    const std::uint64_t done = matte_host_->doneSeq();
    if (done != 0 && done != last_map_sample_seq_) {
        last_map_sample_seq_ = done;
        const float map_ms = matte_host_->hostMapMs();
        if (map_ms >= kSlowMapMs) {
            ++slow_map_streak_;
        } else {
            slow_map_streak_ = 0;
            if (stressed_ && map_ms > 0.1f && map_ms < 20.0f && now >= cooldown_until_) {
                stressed_ = false;
                removerLog("matte_unstressed (map_ms healthy again)");
            }
        }
        if (slow_map_streak_ >= kSlowMapStreak && now >= cooldown_until_) {
            slow_map_streak_ = 0;
            ++cooldown_count_;
            stressed_ = true;
            cooldown_until_ = now + std::chrono::milliseconds(static_cast<int>(kCooldownMs));
            removerLog(std::format(
                "matte_cooldown {:.0f}ms — map_ms={:.1f} infer_ms={:.1f} (skip kicks; "
                "live+last-matte continues) #{}",
                kCooldownMs, map_ms, matte_host_->hostInferMs(), cooldown_count_));
            if (cooldown_count_ >= 2u) {
                (void)matte_host_->restartHost("repeated slow shared Map under outputs");
                cooldown_count_ = 0;
                pushed_quality_ = -1;
                last_map_sample_seq_ = 0;
            }
        }
    }

    const bool in_cooldown = now < cooldown_until_;
    if (in_cooldown || !matte_host_->hostReady() || matte_host_->busy()) {
        if (!matte_host_->hostReady() && (render_ticks_ % 60u) == 1u) {
            removerLog("waiting GenesisRemoverMatteHost.exe ready");
        }
        return;
    }

    const std::uint32_t cap_max_w = stressed_ ? 320u : 480u;
    double min_kick_ms = stressed_ ? kStressedKickMs : kMinKickMs;
    const float infer = matte_host_->hostInferMs();
    if (infer > 1.0f) {
        min_kick_ms = std::max(min_kick_ms, static_cast<double>(infer) + 10.0);
    }
    if (min_kick_ms > 150.0) {
        min_kick_ms = 150.0;
    }

    const double since_kick_ms =
        last_kick_wall_.time_since_epoch().count() == 0
            ? 1e9
            : std::chrono::duration<double, std::milli>(now - last_kick_wall_).count();
    if (since_kick_ms < min_kick_ms) {
        return;
    }

    const std::uint32_t cap_w = width_ > cap_max_w ? cap_max_w : width_;
    const std::uint32_t cap_h =
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(height_) * cap_w) / width_);
    if (cap_h == 0) {
        return;
    }
    if (d3d_capture_share_->width() != cap_w || d3d_capture_share_->height() != cap_h) {
        d3d_capture_share_->allocate(cap_w, cap_h, Format::R8G8B8A8_UNORM, /*is_target=*/true,
                                     /*publish_share=*/true);
    }
    passthrough_->draw(d3d_->context(), d3d_video_in_->srv(), d3d_capture_share_->rtv(), cap_w,
                       cap_h, /*flip_v=*/false);
    if (matte_host_->kickCapture(*d3d_capture_share_, cap_w, cap_h)) {
        last_capture_kick_tick_ = render_ticks_;
        last_kick_wall_ = now;
        if ((render_ticks_ % 60u) == 1u) {
            removerLog(std::format(
                "matte_kick {}x{} min_kick_ms={:.0f} map_ms={:.1f} stressed={} cooldown=0",
                cap_w, cap_h, min_kick_ms, matte_host_->hostMapMs(), stressed_ ? 1 : 0));
        }
    }
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
        removerLog(std::format("render vin-ready ticks={} host={} (no device fence)",
                               render_ticks_,
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

    // Pull latest matte if ready; present path never waits on Map/infer.
    std::uint32_t mw = 0;
    std::uint32_t mh = 0;
    ::genesis::RemoverFrameDebug host_dbg{};
    if (matte_host_ && matte_host_->tryTakeMatte(host_matte_rgba_, mw, mh, &host_dbg) &&
        mw > 0 && mh > 0 && !host_matte_rgba_.empty()) {
        d3d_matte_tex_->allocate(mw, mh, Format::R8G8B8A8_UNORM, /*is_target=*/true,
                                 /*publish_share=*/false);
        if (d3d_matte_tex_->uploadRgba8UpdateSubresource(host_matte_rgba_.data(), mw, mh)) {
            matte_live_ = true;
            dbg = host_dbg;
            if ((render_ticks_ % 30u) == 1u) {
                removerLog(std::format("applied host matte nz={} {}x{}", dbg.nonzero_rgb, mw, mh));
            }
        }
    }

    ID3D11ShaderResourceView* live_srv = d3d_video_in_->srv();
    const double delay_ms = camera_delay_ms_ ? camera_delay_ms_->value() : 0.0;
    if (live_srv && delay_ms > 0.5) {
        live_srv = live_delay_.pushAndSample(*d3d_, *passthrough_, d3d_video_in_->srv(), width_,
                                             height_, delay_ms);
        if ((render_ticks_ % 60u) == 1u) {
            removerLog(std::format("camera_delay_ms={:.1f}", delay_ms));
        }
    } else {
        live_delay_.reset();
    }

    // Live Video In every present × last matte alpha (ExtOnnxVision: skip frames,
    // never stall the host on model rate).
    if (matte_live_ && d3d_matte_tex_ && d3d_matte_tex_->srv() && live_srv) {
        d3d_output_->clear(0.0f, 0.0f, 0.0f, 1.0f);
        passthrough_->drawLiveMasked(d3d_->context(), live_srv, d3d_matte_tex_->srv(),
                                     d3d_output_->rtv(), width_, height_, /*flip_v=*/true);
        dbg.width = width_;
        dbg.height = height_;
        dbg.has_video_in = true;
        dbg.download_ok = true;
        dbg.matte_copied = true;
        dbg.stage = "matte";
    } else {
        d3d_output_->clear(0.0f, 0.0f, 0.0f, 1.0f);
        passthrough_->draw(d3d_->context(), live_srv ? live_srv : d3d_video_in_->srv(),
                           d3d_output_->rtv(), width_, height_, /*flip_v=*/true);
        passthrough_->drawHeartbeat(d3d_->context(), d3d_output_->rtv(), width_, height_,
                                    render_ticks_);
        dbg.width = width_;
        dbg.height = height_;
        dbg.has_video_in = true;
        dbg.stage = "passthrough";
    }

    if (render_ticks_ <= 8u || (render_ticks_ % 60u) == 1u) {
        removerLog(std::format(
            "gpu_present {} {}x{} matte_live={} busy={} map_ms={:.1f} infer_ms={:.0f} "
            "cooldown={} stressed={}",
            matte_live_ ? "live+mask" : "live", width_, height_, matte_live_ ? 1 : 0,
            matte_host_ && matte_host_->busy() ? 1 : 0,
            matte_host_ ? matte_host_->hostMapMs() : 0.0f,
            matte_host_ ? matte_host_->hostInferMs() : 0.0f,
            std::chrono::steady_clock::now() < cooldown_until_ ? 1 : 0, stressed_ ? 1 : 0));
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
