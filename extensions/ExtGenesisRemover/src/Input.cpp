#include "Input.h"

#include "ModulePaths.h"
#include "common/string.h"
#include "matting/BgRemover.hpp"
#include "platform/Paths.hpp"
#include "renderer/vulkan/ExternalRenderTarget.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace rxext::genesis_remover {

namespace {
#if defined(_WIN32)
constexpr auto kHandleType = HandleType::OPAQUE_WIN32;
#else
constexpr auto kHandleType = HandleType::OPAQUE_FD;
#endif

std::mutex g_log_mutex;

void appendInitLog(const std::string& line) {
    try {
        const auto dir = extensionModuleDirectory();
        if (dir.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(g_log_mutex);
        std::ofstream out(dir / "remover-init.log", std::ios::app);
        if (!out) {
            return;
        }
        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch())
                            .count();
        out << ms << " " << line << '\n';
    } catch (...) {
        // Never throw from diagnostics.
    }
}

} // namespace

Input::Input(std::uint64_t adapter_luid, ValueSet settings)
    : adapter_luid_(adapter_luid), settings_(std::move(settings)) {
    const std::filesystem::path module_dir = extensionModuleDirectory();
    const std::filesystem::path roots[] = {module_dir, module_dir / "assets"};
    ::genesis::setAssetSearchRoots(roots);

    // Match ExtGenesis / ExtSampleD3D parameter surface expected by PIXERA timeline.
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
}

Input::~Input() = default;

bool Input::initialize() noexcept try {
    // Use const char* default so ValueSet::get returns std::string (MSVC std::format
    // cannot format rxext::string / ptl::string).
    const std::string handle = settings_.get(SettingNames::handle, "main");
    appendInitLog(std::format("initialize begin handle='{}' adapter_luid={:016x}", handle,
                              adapter_luid_));
    host().log_info(std::format("initializing GENESIS Remover stream '{}' (adapter_luid={:016x})",
                                handle, adapter_luid_));
    if (!update_settings(std::move(settings_))) {
        appendInitLog("initialize FAILED: update_settings returned false");
        return false;
    }
    appendInitLog(std::format("initialize OK {}x{} output_ready={}", width_, height_,
                              pipeline_ && pipeline_->hasOutput()));
    return true;
} catch (const std::exception& ex) {
    appendInitLog(std::format("initialize exception: {}", ex.what()));
    host().log_error(std::format("GENESIS Remover init failed: {}", ex.what()));
    return false;
}

bool Input::update_settings(ValueSet settings) noexcept try {
    settings_ = std::move(settings);
    width_ = settings_.get<std::uint32_t>(SettingNames::resolution_x, 1920);
    height_ = settings_.get<std::uint32_t>(SettingNames::resolution_y, 1080);

    // PIXERA often injects the real GPU LUID on the stream, not only the device.
    if (const string stream_luid = settings_.get(SettingNames::adapter_luid, string{});
        !stream_luid.empty()) {
        adapter_luid_ = common::from_hex_string<std::uint64_t>(stream_luid);
    }

    appendInitLog(std::format("update_settings {}x{} adapter_luid={:016x}", width_, height_,
                              adapter_luid_));

    if (pipeline_ && pipeline_adapter_luid_ != adapter_luid_) {
        host().log_info(std::format("GENESIS Remover recreating pipeline for adapter {:016x}",
                                    adapter_luid_));
        appendInitLog("recreating pipeline for new adapter");
        pipeline_.reset();
        processing_enabled_ = false;
    }

    // Timeline-safe path (mirrors ExtSampleD3D): publish shared OUTPUT sampler only.
    // Do NOT create video_in, load ONNX, or start async during initialize().
    ensurePipeline();
    pipeline_->resize(width_, height_);
    publishOutput();

    host().log_info(std::format("GENESIS Remover output ready {}x{} (matte deferred)", width_,
                                height_));
    appendInitLog(std::format("output published {}x{}", width_, height_));
    return true;
} catch (const std::exception& ex) {
    appendInitLog(std::format("update_settings exception: {}", ex.what()));
    host().log_error(std::format("GENESIS Remover settings failed: {}", ex.what()));
    return false;
}

void Input::ensurePipeline() {
    if (pipeline_) {
        return;
    }
    try {
        appendInitLog(std::format("creating RemoverPipeline adapter={:016x}", adapter_luid_));
        pipeline_ = std::make_unique<::genesis::RemoverPipeline>(adapter_luid_);
        pipeline_adapter_luid_ = adapter_luid_;
        appendInitLog("RemoverPipeline VulkanContext OK");
    } catch (const std::exception& ex) {
        if (adapter_luid_ == 0) {
            throw;
        }
        host().log_warning(std::format(
            "GENESIS Remover pipeline failed for adapter {:016x} ({}), retrying default GPU",
            adapter_luid_, ex.what()));
        appendInitLog(std::format("pipeline adapter fail ({}); retry LUID 0", ex.what()));
        pipeline_ = std::make_unique<::genesis::RemoverPipeline>(0);
        pipeline_adapter_luid_ = 0;
        adapter_luid_ = 0;
        appendInitLog("RemoverPipeline VulkanContext OK (default GPU)");
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
    appendInitLog(std::format("processing enabled status={}", pipeline_->remover().status()));
}

void Input::publishOutput() {
    if (!pipeline_ || !sampler_) {
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
}

void Input::publishVideoIn() {
    if (!video_in_ || !pipeline_) {
        return;
    }
    update_input_texture(*video_in_, [&](const TextureDesc& desc) {
        width_ = static_cast<std::uint32_t>(desc.width);
        height_ = static_cast<std::uint32_t>(desc.height);
        appendInitLog(std::format("video_in allocate {}x{}", width_, height_));
        // Size output first, then create matching video_in (never during initialize).
        pipeline_->resize(width_, height_);
        pipeline_->ensureVideoIn();
        const auto& exp = pipeline_->videoIn().exportInfo();
        TextureDesc shared = desc;
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
        publishOutput();
        enableProcessing();
    });
}

ValueSet Input::get_state() noexcept {
    ValueSet state;
    int res_x = static_cast<int>(width_);
    int res_y = static_cast<int>(height_);
    Format format = Format::R8G8B8A8_UNORM;
    if (sampler_) {
        const auto desc = sampler_->texture().desc();
        if (desc.width > 0 && desc.height > 0) {
            res_x = desc.width;
            res_y = desc.height;
            format = desc.format;
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
    if (!pipeline_) {
        return true;
    }
    // Match ExtSampleD3D: allocate input shared texture only after stream is alive.
    publishVideoIn();
    return true;
} catch (const std::exception& ex) {
    appendInitLog(std::format("update exception: {}", ex.what()));
    host().log_error(std::format("GENESIS Remover update failed: {}", ex.what()));
    return false;
}

SyncDesc Input::before_render() noexcept {
    return {};
}

RenderResult Input::render() noexcept try {
    if (!pipeline_) {
        host().log_error("GENESIS Remover render failed: pipeline missing");
        return RenderResult::Failed;
    }
    // If video is connected, enable matte; otherwise keep publishing transparent frames.
    if (pipeline_->hasVideoIn() && !processing_enabled_) {
        enableProcessing();
    }
    std::string error;
    if (!pipeline_->processFrame(&error)) {
        host().log_error(std::format("GENESIS Remover frame failed: {}", error));
        return RenderResult::Failed;
    }
    return RenderResult::Succeeded;
} catch (const std::exception& ex) {
    appendInitLog(std::format("render exception: {}", ex.what()));
    host().log_error(std::format("GENESIS Remover render failed: {}", ex.what()));
    return RenderResult::Failed;
}

SyncDesc Input::after_render() noexcept {
    return {};
}

} // namespace rxext::genesis_remover
