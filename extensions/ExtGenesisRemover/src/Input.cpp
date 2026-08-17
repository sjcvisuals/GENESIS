#include "WinPrefix.h"
#include "Input.h"

#include "ModulePaths.h"
#include "RemoverLog.h"
#include "common/string.h"
#include "matting/BgRemover.hpp"
#include "platform/Paths.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <volk.h>

namespace rxext::genesis_remover {

namespace {
#if defined(_WIN32)
constexpr auto kHandleType = HandleType::OPAQUE_WIN32;
#else
constexpr auto kHandleType = HandleType::OPAQUE_FD;
#endif

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

Input::Input(std::uint64_t adapter_luid, ValueSet settings)
    : adapter_luid_(adapter_luid), settings_(std::move(settings)) {
    // Match ExtGenesis: register parameters in the constructor (before initialize).
    const std::filesystem::path module_dir = extensionModuleDirectory();
    const std::filesystem::path roots[] = {module_dir, module_dir / "assets"};
    ::genesis::setAssetSearchRoots(roots);

    sampler_ = add_output_parameter<ParameterTexture>(ParameterNames::sampler);

    time_ = add_parameter<ParameterValue>("time");
    time_->set_property(PropertyNames::purpose, PurposeNames::TimelineTime);

    // Parked playhead still needs update() so Video In can allocate after link.
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

    removerLog(std::format("Input ctor ExtGenesis-parity adapter_luid={:016x}", adapter_luid_));
}

Input::~Input() {
    removerLog("Input dtor");
}

void Input::ensureRemover() {
    if (remover_) {
        return;
    }
    remover_ = std::make_unique<::genesis::RemoverPipeline>(adapter_luid_);
    removerLog(std::format("RemoverPipeline ready adapter={:016x}", adapter_luid_));
    host().log_info(std::format("GENESIS Remover Vulkan ready (adapter_luid={:016x}, matte deferred)",
                                adapter_luid_));
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
    if (!remover_ || !sampler_ || !remover_->hasOutput()) {
        return;
    }
    const auto& exp = remover_->output().colorExport();
    if (!exp.win32_handle) {
        throw std::runtime_error("GENESIS Remover output share handle is null");
    }
    TextureDesc desc{};
    desc.width = remover_->output().extent().width;
    desc.height = remover_->output().extent().height;
    desc.format = Format::R8G8B8A8_UNORM;
    desc.is_target = true;
    desc.share_handle = {
        kHandleType,
        reinterpret_cast<Handle>(exp.win32_handle),
        0,
        exp.memory_size,
        0,
        exp.dedicated,
    };
    sampler_->set_texture(host().create_texture(desc));
    removerLog(std::format("shared OUTPUT {}x{} mem={}", desc.width, desc.height, exp.memory_size));
}

void Input::publishVideoInTexture() {
    if (!video_in_ || !remover_) {
        return;
    }

    // Exact ExtGenesis::publishVideoInTexture allocate sequence.
    auto allocate = [&](const TextureDesc& desc) {
        if (desc.width == 0 || desc.height == 0) {
            return;
        }
        width_ = static_cast<std::uint32_t>(desc.width);
        height_ = static_cast<std::uint32_t>(desc.height);
        removerLog(std::format("video_in allocate {}x{} format={}", width_, height_,
                               get_format_name(desc.format)));
        remover_->resize(width_, height_);
        const VkFormat vk_fmt = [&]() -> VkFormat {
            switch (desc.format) {
            case Format::B8G8R8A8_UNORM:
                return VK_FORMAT_B8G8R8A8_UNORM;
            case Format::R16G16B16A16_SFLOAT:
                return VK_FORMAT_R16G16B16A16_SFLOAT;
            case Format::R32G32B32A32_SFLOAT:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            default:
                return VK_FORMAT_R8G8B8A8_UNORM;
            }
        }();
        remover_->ensureVideoIn(vk_fmt);
        const auto& exp = remover_->videoIn().exportInfo();
        if (!exp.win32_handle) {
            throw std::runtime_error("GENESIS Remover video_in share handle is null");
        }
        TextureDesc shared = desc;
        if (shared.format == Format::None) {
            shared.format = Format::R8G8B8A8_UNORM;
        }
        // Advertise the format we actually allocated (must match Vulkan image + mem size).
        shared.is_target = false;
        shared.share_handle = {
            kHandleType,
            reinterpret_cast<Handle>(exp.win32_handle),
            0,
            exp.memory_size,
            0,
            exp.dedicated,
        };
        video_in_->set_texture(host().create_texture(shared));
        publishTargetTexture();
        applyMatteSettings();
        remover_->setAsyncEnabled(true);
        removerLog(std::format("shared VIDEO_IN {}x{} format={} vk={} mem={}", shared.width,
                               shared.height, get_format_name(shared.format),
                               static_cast<int>(vk_fmt), exp.memory_size));
    };

    // Official helper: fires only when host sets resolution_x/y AND format.
    update_input_texture(*video_in_, allocate);

    // ExtGenesis narrow fallback when format property is missing but size is known.
    if (!remover_->hasVideoIn()) {
        TextureDesc desc{};
        desc.width = static_cast<std::size_t>(
            video_in_->get_property<int>(SettingNames::resolution_x, 0));
        desc.height = static_cast<std::size_t>(
            video_in_->get_property<int>(SettingNames::resolution_y, 0));
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
            removerLog("video_in allocate via ExtGenesis fallback");
            allocate(desc);
        } else if ((render_ticks_ % 60u) == 1u) {
            removerLog(std::format(
                "waiting Video In link (props {}x{} format='{}') - cyan is correct until then",
                video_in_->get_property<int>(SettingNames::resolution_x, 0),
                video_in_->get_property<int>(SettingNames::resolution_y, 0),
                std::string(video_in_->get_property(SettingNames::format))));
        }
    }
}

void Input::logStatusMaybe(const ::genesis::RemoverFrameDebug& dbg) {
    if (debug_stage_) {
        debug_stage_->set_value(stageToEnum(dbg.stage));
    }
    const std::string ui = std::format("v17 {} nz={} mid=({},{},{}) vin={}",
                                       dbg.stage ? dbg.stage : "?", dbg.nonzero_rgb, dbg.mid_r,
                                       dbg.mid_g, dbg.mid_b, dbg.has_video_in ? 1 : 0);
    if (debug_status_) {
        debug_status_->set_value(ui);
    }
    if (last_stage_ != (dbg.stage ? dbg.stage : "") || (render_ticks_ % 30u) == 1u) {
        last_stage_ = dbg.stage ? dbg.stage : "";
        removerLog(std::format(
            "STATUS stage={} vin={} dl={} nz={} mid=({},{},{},{}) {}x{} matte={} luid={:016x}",
            dbg.stage ? dbg.stage : "?", dbg.has_video_in ? 1 : 0, dbg.download_ok ? 1 : 0,
            dbg.nonzero_rgb, dbg.mid_r, dbg.mid_g, dbg.mid_b, dbg.mid_a, dbg.width, dbg.height,
            dbg.matte_copied ? 1 : 0, adapter_luid_));
    }
}

bool Input::initialize() noexcept try {
    const std::string handle = settings_.get(SettingNames::handle, "main");
    removerLog(std::format("initialize begin handle='{}' adapter_luid={:016x} ({})", handle,
                           adapter_luid_, kRemoverBuildStamp));
    host().log_info(std::format("initializing GENESIS Remover stream '{}' ({})", handle,
                                kRemoverBuildStamp));
    // ExtGenesis: initialize delegates to update_settings (shared output only).
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

    // Timeline-safe (ExtGenesis): publish shared output only. Video In in update().
    ensureRemover();
    remover_->resize(width_, height_);
    publishTargetTexture();
    if (debug_status_) {
        debug_status_->set_value(std::string("v16 shared output ready - link Video In"));
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
        // ExtGenesis always returns true for Remover mode.
        return string("true");
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
    // Texture publish stays in update() only (ExtGenesis parity).

    if (remover_->hasVideoIn() && !remover_->asyncEnabled()) {
        applyMatteSettings();
        remover_->setAsyncEnabled(true);
    }

    ::genesis::RemoverFrameDebug dbg{};
    std::string error;
    if (!remover_->processFrame(&error, &dbg, /*show_diagnostics=*/true)) {
        if (!error.empty() && (render_ticks_ % 60u) == 1u) {
            removerLog(std::format("processFrame: {}", error));
        }
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
