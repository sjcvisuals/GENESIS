#include "Input.h"

#include "ModulePaths.h"
#include "RemoverLog.h"
#include "common/string.h"
#include "matting/BgRemover.hpp"
#include "platform/Paths.hpp"
#include "renderer/vulkan/ExternalRenderTarget.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

namespace rxext::genesis_remover {

namespace {
#if defined(_WIN32)
constexpr auto kHandleType = HandleType::OPAQUE_WIN32;
#else
constexpr auto kHandleType = HandleType::OPAQUE_FD;
#endif
} // namespace

Input::Input(std::uint64_t adapter_luid, ValueSet settings)
    : adapter_luid_(adapter_luid), settings_(std::move(settings)) {
    // HostContext is not injected yet (see rxext_client InputStream ctor flow).
    // Only store settings here — register parameters + textures in initialize().
    removerLog(std::format("Input ctor adapter_luid={:016x}", adapter_luid_));
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
    removerLog("parameters registered");
}

void Input::publishPlaceholderSampler() {
    if (!sampler_) {
        return;
    }
    // ExtSampleCPU pattern: host-owned texture, no Vulkan / share handle.
    // Guarantees PIXERA can accept the stream even if GPU init fails later.
    TextureDesc desc{};
    desc.width = static_cast<std::size_t>(std::max<std::uint32_t>(width_, 2));
    desc.height = static_cast<std::size_t>(std::max<std::uint32_t>(height_, 2));
    desc.format = Format::R8G8B8A8_UNORM;
    desc.is_target = true;
    sampler_->set_texture(host().create_texture(desc));

    // Transparent black so timeline compositing stays clean until GPU path is up.
    const std::size_t pixels = desc.width * desc.height;
    std::vector<std::uint32_t> zeros(pixels, 0u);
    BufferDesc buffer{};
    buffer.data = zeros.data();
    buffer.pitch = desc.width * sizeof(std::uint32_t);
    buffer.size = buffer.pitch * desc.height;
    host().upload_texture(sampler_->texture(), buffer, true, []() noexcept {});
    placeholder_published_ = true;
    shared_output_published_ = false;
    removerLog(std::format("placeholder sampler {}x{}", desc.width, desc.height));
}

bool Input::initialize() noexcept try {
    const std::string handle = settings_.get(SettingNames::handle, "main");
    removerLog(std::format("initialize begin handle='{}' adapter_luid={:016x}", handle,
                           adapter_luid_));
    host().log_info(std::format("initializing GENESIS Remover stream '{}' ({})", handle,
                                kRemoverBuildStamp));

    registerParameters();

    width_ = settings_.get<std::uint32_t>(SettingNames::resolution_x, 1920);
    height_ = settings_.get<std::uint32_t>(SettingNames::resolution_y, 1080);
    if (const string stream_luid = settings_.get(SettingNames::adapter_luid, string{});
        !stream_luid.empty()) {
        adapter_luid_ = common::from_hex_string<std::uint64_t>(stream_luid);
    }

    // CRITICAL: accept the timeline stream with a CPU placeholder.
    // Do not create Vulkan/ONNX here — that is what previously made PIXERA show
    // the generic "dongle / corrupted / path" dialog when initialize() failed.
    publishPlaceholderSampler();
    removerLog("initialize OK (cpu-first placeholder)");
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
        gpu_failed_ = false;
    }

    // Keep stream alive: refresh placeholder if GPU path is not up yet.
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
    host().log_error(std::format("GENESIS Remover settings failed: {}", ex.what()));
    // Still return true with placeholder so PIXERA does not unload the clip.
    try {
        publishPlaceholderSampler();
        return true;
    } catch (...) {
        return false;
    }
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
        removerLog("RemoverPipeline GPU output published");
        host().log_info(std::format("GENESIS Remover GPU ready {}x{}", width_, height_));
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
                return false;
            }
        }
        removerLog(std::format("GPU pipeline failed: {}", ex.what()));
        host().log_error(std::format("GENESIS Remover GPU unavailable: {}", ex.what()));
        pipeline_.reset();
        gpu_failed_ = true;
        publishPlaceholderSampler();
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
    if (matte.backend == ::genesis::BgRemoverBackend::Auto && matte.quality == next_q) {
        return;
    }
    matte.backend = ::genesis::BgRemoverBackend::Auto;
    matte.quality = next_q;
    pipeline_->setSettings(matte);
}

void Input::enableProcessing() {
    if (!pipeline_ || processing_enabled_) {
        return;
    }
    applyMatteSettings();
    pipeline_->setAsyncEnabled(true);
    processing_enabled_ = true;
    host().log_info(std::format("GENESIS Remover processing enabled ({})",
                                pipeline_->remover().status()));
    removerLog(std::format("processing enabled status={}", pipeline_->remover().status()));
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
}

void Input::publishVideoIn() {
    if (!video_in_ || !pipeline_) {
        return;
    }

    auto allocate = [&](const TextureDesc& desc) {
        if (desc.width == 0 || desc.height == 0) {
            return;
        }
        width_ = static_cast<std::uint32_t>(desc.width);
        height_ = static_cast<std::uint32_t>(desc.height);
        removerLog(std::format("video_in allocate {}x{} format={}", width_, height_,
                               get_format_name(desc.format)));
        pipeline_->resize(width_, height_);
        pipeline_->ensureVideoIn();
        const auto& exp = pipeline_->videoIn().exportInfo();
        TextureDesc shared = desc;
        if (shared.format == Format::None) {
            shared.format = Format::R8G8B8A8_UNORM;
        }
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
        publishSharedOutput();
        enableProcessing();
    };

    // SDK helper — only fires when resolution_* AND format properties are set.
    update_input_texture(*video_in_, allocate);

    // Fallback: PIXERA sometimes sets resolution without format, which makes the
    // helper no-op forever (black output). Also accept an already-sized host texture.
    if (!pipeline_->hasVideoIn()) {
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
            removerLog("video_in allocate via fallback (missing format property?)");
            allocate(desc);
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
                res_x = desc.width;
                res_y = desc.height;
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
    // Soft-fail GPU: never return false just because Vulkan/ONNX is unavailable.
    if (!tryEnsureGpuPipeline()) {
        return true;
    }
    publishVideoIn();
    return true;
} catch (const std::exception& ex) {
    removerLog(std::format("update exception: {}", ex.what()));
    host().log_error(std::format("GENESIS Remover update failed: {}", ex.what()));
    return true;
}

SyncDesc Input::before_render() noexcept {
    return {};
}

RenderResult Input::render() noexcept try {
    if (!pipeline_) {
        return RenderResult::Succeeded;
    }
    if (pipeline_->hasVideoIn() && !processing_enabled_) {
        enableProcessing();
    }
    std::string error;
    if (!pipeline_->processFrame(&error)) {
        host().log_error(std::format("GENESIS Remover frame failed: {}", error));
        removerLog(std::format("processFrame failed: {}", error));
        return RenderResult::Succeeded;
    }
    return RenderResult::Succeeded;
} catch (const std::exception& ex) {
    removerLog(std::format("render exception: {}", ex.what()));
    host().log_error(std::format("GENESIS Remover render failed: {}", ex.what()));
    return RenderResult::Succeeded;
}

SyncDesc Input::after_render() noexcept {
    return {};
}

} // namespace rxext::genesis_remover
