#include "WinPrefix.h"
#include "Input.h"

#include "ModulePaths.h"
#include "RemoverLog.h"
#include "common/string.h"
#include "matting/BgRemover.hpp"
#include "platform/Paths.hpp"
#include "renderer/vulkan/ExternalRenderTarget.hpp"

#include <volk.h>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace rxext::genesis_remover {

namespace {
#if defined(_WIN32)
constexpr auto kHandleType = HandleType::OPAQUE_WIN32;
#else
constexpr auto kHandleType = HandleType::OPAQUE_FD;
#endif

int stageToEnum(const char* stage) {
    if (!stage) return 0;
    if (std::string_view(stage) == "waiting_video_in") return 1;
    if (std::string_view(stage) == "download_fail") return 2;
    if (std::string_view(stage) == "input_black") return 3;
    if (std::string_view(stage) == "passthrough") return 4;
    if (std::string_view(stage) == "matte") return 5;
    if (std::string_view(stage) == "error") return 6;
    return 0;
}
} // namespace

Input::Input(std::uint64_t adapter_luid, ValueSet settings)
    : adapter_luid_(adapter_luid), settings_(std::move(settings)) {
    removerLog(std::format("Input ctor shared-vulkan adapter_luid={:016x}", adapter_luid_));
}

Input::~Input() {
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

    // Keep update() alive when playhead is parked (needed after Video In link).
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

    params_registered_ = true;
    removerLog("parameters registered (shared-vulkan + debug)");
}

void Input::setDebugStatus(const std::string& text) {
    if (debug_status_ && text != last_status_) {
        last_status_ = text;
        debug_status_->set_value(text);
    }
}

void Input::logStatusMaybe(const ::genesis::RemoverFrameDebug& dbg) {
    if (debug_stage_) {
        debug_stage_->set_value(stageToEnum(dbg.stage));
    }
    const std::string line = std::format(
        "STATUS stage={} vin={} dl={} nz={} mid=({},{},{},{}) {}x{} matte={} proc={} shared_out={} "
        "luid={:016x}",
        dbg.stage ? dbg.stage : "?", dbg.has_video_in ? 1 : 0, dbg.download_ok ? 1 : 0,
        dbg.nonzero_rgb, dbg.mid_r, dbg.mid_g, dbg.mid_b, dbg.mid_a, dbg.width, dbg.height,
        dbg.matte_copied ? 1 : 0, processing_enabled_ ? 1 : 0, shared_output_published_ ? 1 : 0,
        adapter_luid_);
    setDebugStatus(line);
    if (last_stage_ != (dbg.stage ? dbg.stage : "") || (render_ticks_ % 60u) == 1u) {
        last_stage_ = dbg.stage ? dbg.stage : "";
        removerLog(line);
    }
}

void Input::publishPlaceholderSampler() {
    if (!sampler_) {
        return;
    }
    TextureDesc desc{};
    desc.width = static_cast<std::size_t>(std::max<std::uint32_t>(width_, 2));
    desc.height = static_cast<std::size_t>(std::max<std::uint32_t>(height_, 2));
    desc.format = Format::R8G8B8A8_UNORM;
    desc.is_target = true;
    sampler_->set_texture(host().create_texture(desc));

    // Host cyan until shared Vulkan output is published.
    const std::size_t bytes = desc.width * desc.height * 4u;
    std::vector<std::uint8_t> cyan(bytes);
    for (std::size_t i = 0; i + 3 < bytes; i += 4) {
        cyan[i + 0] = 0;
        cyan[i + 1] = 220;
        cyan[i + 2] = 255;
        cyan[i + 3] = 255;
    }
    BufferDesc buffer{};
    buffer.data = cyan.data();
    buffer.pitch = desc.width * 4u;
    buffer.size = bytes;
    host().upload_texture(sampler_->texture(), buffer, true, []() noexcept {});
    placeholder_published_ = true;
    shared_output_published_ = false;
    removerLog(std::format("placeholder sampler {}x{} (host cyan)", desc.width, desc.height));
    setDebugStatus("placeholder host cyan — GPU not up yet");
}

bool Input::initialize() noexcept try {
    const std::string handle = settings_.get(SettingNames::handle, "main");
    removerLog(std::format("initialize begin handle='{}' adapter_luid={:016x} ({})", handle,
                           adapter_luid_, kRemoverBuildStamp));
    host().log_info(std::format("initializing GENESIS Remover stream '{}' ({})", handle,
                                kRemoverBuildStamp));

    registerParameters();

    width_ = settings_.get<std::uint32_t>(SettingNames::resolution_x, 1920);
    height_ = settings_.get<std::uint32_t>(SettingNames::resolution_y, 1080);
    if (const string stream_luid = settings_.get(SettingNames::adapter_luid, string{});
        !stream_luid.empty()) {
        adapter_luid_ = common::from_hex_string<std::uint64_t>(stream_luid);
    }

    // Accept timeline with host cyan first — Vulkan/ONNX deferred to update().
    publishPlaceholderSampler();
    removerLog("initialize OK (cpu-first placeholder, shared-vulkan deferred)");
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
    if (const string stream_luid = settings_.get(SettingNames::adapter_luid, string{});
        !stream_luid.empty()) {
        adapter_luid_ = common::from_hex_string<std::uint64_t>(stream_luid);
    }
    removerLog(std::format("update_settings {}x{} adapter={:016x}", width_, height_,
                           adapter_luid_));

    if (pipeline_ && pipeline_adapter_luid_ != adapter_luid_) {
        pipeline_.reset();
        processing_enabled_ = false;
        shared_output_published_ = false;
        video_in_published_ = false;
        gpu_failed_ = false;
        matte_live_ = false;
    }

    if (!shared_output_published_) {
        if (!params_registered_) {
            registerParameters();
        }
        publishPlaceholderSampler();
    } else if (pipeline_) {
        try {
            pipeline_->resize(width_, height_);
            publishSharedOutput();
        } catch (const std::exception& ex) {
            removerLog(std::format("resize/publish after settings failed: {}", ex.what()));
            publishPlaceholderSampler();
        }
    }
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("update_settings exception: {}", ex.what()));
    try {
        publishPlaceholderSampler();
    } catch (...) {
    }
    return true;
}

bool Input::tryEnsureGpuPipeline() {
    if (pipeline_) {
        return true;
    }
    if (gpu_failed_) {
        return false;
    }
    try {
        removerLog(std::format("creating RemoverPipeline adapter={:016x}", adapter_luid_));
        pipeline_ = std::make_unique<::genesis::RemoverPipeline>(adapter_luid_);
        pipeline_adapter_luid_ = adapter_luid_;
        pipeline_->resize(width_, height_);
        publishSharedOutput();
        removerLog(std::format("RemoverPipeline GPU output published {}x{} handle={}", width_,
                               height_,
                               pipeline_->output().colorExport().win32_handle ? "ok" : "NULL"));
        host().log_info(std::format("GENESIS Remover GPU ready {}x{}", width_, height_));
        setDebugStatus("shared output published — waiting Video In");
        return true;
    } catch (const std::exception& ex) {
        if (adapter_luid_ != 0) {
            removerLog(std::format("pipeline adapter fail ({}); retry LUID 0", ex.what()));
            try {
                pipeline_ = std::make_unique<::genesis::RemoverPipeline>(0);
                pipeline_adapter_luid_ = 0;
                adapter_luid_ = 0;
                pipeline_->resize(width_, height_);
                publishSharedOutput();
                removerLog("RemoverPipeline GPU output published (default GPU)");
                return true;
            } catch (const std::exception& ex2) {
                removerLog(std::format("GPU pipeline failed: {}", ex2.what()));
                host().log_error(std::format("GENESIS Remover GPU unavailable: {}", ex2.what()));
                pipeline_.reset();
                gpu_failed_ = true;
                publishPlaceholderSampler();
                setDebugStatus(std::format("GPU FAIL: {}", ex2.what()));
                return false;
            }
        }
        removerLog(std::format("GPU pipeline failed: {}", ex.what()));
        host().log_error(std::format("GENESIS Remover GPU unavailable: {}", ex.what()));
        pipeline_.reset();
        gpu_failed_ = true;
        publishPlaceholderSampler();
        setDebugStatus(std::format("GPU FAIL: {}", ex.what()));
        return false;
    }
}

void Input::applyMatteSettings() {
    if (!pipeline_) {
        return;
    }
    ::genesis::BgRemoverSettings matte = pipeline_->remover().settings();
    const int q = quality_ ? std::clamp(quality_->value(), 0, 2) : 1;
    const auto next_q = static_cast<::genesis::BgRemoverQuality>(q);
    matte.backend = ::genesis::BgRemoverBackend::Auto;
    matte.quality = next_q;
    matte.rvm_downsample = 0.0f;
    matte.temporal_alpha = 0.20f;
    pipeline_->setSettings(matte);
}

void Input::enableProcessing() {
    if (!pipeline_ || processing_enabled_) {
        return;
    }
    applyMatteSettings();
    pipeline_->setAsyncEnabled(true);
    processing_enabled_ = true;
    removerLog(std::format("processing enabled status={} ep={}", pipeline_->remover().status(),
                           pipeline_->asyncWorker().executionProvider()));
    host().log_info(std::format("GENESIS Remover processing enabled ({})",
                                pipeline_->remover().status()));
}

void Input::publishSharedOutput() {
    if (!pipeline_ || !sampler_ || !pipeline_->hasOutput()) {
        return;
    }
    const auto& exp = pipeline_->output().colorExport();
    if (!exp.win32_handle) {
        throw std::runtime_error("GENESIS Remover output share handle is null");
    }
    TextureDesc desc{};
    desc.width = pipeline_->output().extent().width;
    desc.height = pipeline_->output().extent().height;
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
    shared_output_published_ = true;
    placeholder_published_ = false;
    removerLog(std::format("shared OUTPUT published {}x{} mem={} dedicated={}", desc.width,
                           desc.height, exp.memory_size, exp.dedicated ? 1 : 0));
}

void Input::publishVideoIn() {
    if (!video_in_ || !pipeline_) {
        return;
    }

    auto allocate = [&](const TextureDesc& desc, const char* reason) {
        if (desc.width == 0 || desc.height == 0) {
            return;
        }
        width_ = static_cast<std::uint32_t>(desc.width);
        height_ = static_cast<std::uint32_t>(desc.height);
        const Format pixera_fmt =
            desc.format == Format::None ? Format::R8G8B8A8_UNORM : desc.format;
        const VkFormat vk_fmt = [&]() -> VkFormat {
            switch (pixera_fmt) {
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
        removerLog(std::format("video_in allocate {}x{} format={} vk={} ({})", width_, height_,
                               get_format_name(pixera_fmt), static_cast<int>(vk_fmt), reason));
        pipeline_->resize(width_, height_);
        // Must match PIXERA's requested format (often FP16). v14 called ensureVideoIn()
        // with default RGBA8 while advertising FP16 → shared readback stayed black.
        pipeline_->ensureVideoIn(vk_fmt);
        const auto& exp = pipeline_->videoIn().exportInfo();
        if (!exp.win32_handle) {
            removerLog("video_in share handle NULL");
            setDebugStatus("video_in share handle NULL");
            return;
        }
        TextureDesc shared = desc;
        shared.format = pixera_fmt;
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
        video_in_published_ = true;
        publishSharedOutput();
        enableProcessing();
        removerLog(std::format("shared VIDEO_IN published {}x{} format={} mem={}", shared.width,
                               shared.height, get_format_name(shared.format), exp.memory_size));
        setDebugStatus(std::format("shared video_in {}x{} {} — waiting PIXERA write", width_,
                                   height_, get_format_name(shared.format)));
    };

    update_input_texture(*video_in_, [&](const TextureDesc& desc) {
        allocate(desc, "update_input_texture");
    });

    if (!pipeline_->hasVideoIn()) {
        TextureDesc desc{};
        desc.width = static_cast<std::size_t>(
            video_in_->get_property<int>(SettingNames::resolution_x, 0));
        desc.height = static_cast<std::size_t>(
            video_in_->get_property<int>(SettingNames::resolution_y, 0));
        const std::string format_str(video_in_->get_property(SettingNames::format));
        desc.format = get_format_by_name(format_str, Format::R8G8B8A8_UNORM);
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
        if (desc.width == 0 || desc.height == 0) {
            // Same-res link: props may never change — use stream size after a few ticks.
            if (render_ticks_ > 45 && width_ > 0 && height_ > 0) {
                desc.width = width_;
                desc.height = height_;
                // Prefer PIXERA's format property even when dims came from stream defaults.
                desc.format = get_format_by_name(format_str, Format::R16G16B16A16_SFLOAT);
                allocate(desc, "same-res-stream-fallback");
            } else if ((render_ticks_ % 60u) == 1u) {
                removerLog(std::format("waiting Video In props {}x{} format='{}' ticks={}",
                                       video_in_->get_property<int>(SettingNames::resolution_x, 0),
                                       video_in_->get_property<int>(SettingNames::resolution_y, 0),
                                       format_str, render_ticks_));
            }
        } else {
            allocate(desc, format_str.empty() ? "props-fallback" : "props+format");
        }
    }
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
    state.set(StateNames::frame_rate, 50.0);
    return state;
}

string Input::get_property(string_view name) noexcept {
    if (name == PropertyNames::applies_alpha) {
        // Avoid black timeline until we have a covered matte.
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
    if (!tryEnsureGpuPipeline()) {
        return true;
    }
    publishVideoIn();
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

    // PIXERA may skip update — pump GPU + Video In here too.
    if (!tryEnsureGpuPipeline()) {
        return RenderResult::Succeeded;
    }
    publishVideoIn();

    if (pipeline_->hasVideoIn() && !processing_enabled_) {
        enableProcessing();
    }

    ::genesis::RemoverFrameDebug dbg{};
    std::string error;
    const bool ok = pipeline_->processFrame(&error, &dbg, /*show_diagnostics=*/true);
    logStatusMaybe(dbg);

    if (dbg.stage && std::string_view(dbg.stage) == "matte") {
        if (!matte_live_) {
            matte_live_ = true;
            removerLog(std::format("matte_live=1 ep={} status={}",
                                   pipeline_->asyncWorker().executionProvider(),
                                   pipeline_->remover().status()));
        }
    }

    if (!ok && !error.empty() && (render_ticks_ % 60u) == 1u) {
        removerLog(std::format("processFrame: {}", error));
    }
    return RenderResult::Succeeded;
} catch (const std::exception& ex) {
    removerLog(std::format("render exception: {}", ex.what()));
    setDebugStatus(std::format("render exception: {}", ex.what()));
    return RenderResult::Succeeded;
}

SyncDesc Input::after_render() noexcept {
    return {};
}

} // namespace rxext::genesis_remover
