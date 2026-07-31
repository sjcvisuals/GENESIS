#include "Input.h"

#include "ModulePaths.h"
#include "common/string.h"
#include "matting/BgRemover.hpp"
#include "platform/Paths.hpp"
#include "renderer/vulkan/ExternalRenderTarget.hpp"

#include <algorithm>
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
    host().log_info(std::format("initializing GENESIS Remover stream '{}' (adapter_luid={:016x})",
                                handle, adapter_luid_));
    if (!update_settings(std::move(settings_))) {
        return false;
    }
    return true;
} catch (const std::exception& ex) {
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

    if (pipeline_ && pipeline_adapter_luid_ != adapter_luid_) {
        host().log_info(std::format("GENESIS Remover recreating pipeline for adapter {:016x}",
                                    adapter_luid_));
        pipeline_.reset();
    }

    ensurePipeline();
    pipeline_->resize(width_, height_);
    applyMatteSettings();
    publishOutput();
    publishVideoIn();

    // Enable async only after the shared output sampler exists.
    pipeline_->setAsyncEnabled(true);

    host().log_info(std::format("GENESIS Remover ready ({}) {}x{}", pipeline_->remover().status(),
                                width_, height_));
    return true;
} catch (const std::exception& ex) {
    host().log_error(std::format("GENESIS Remover settings failed: {}", ex.what()));
    return false;
}

void Input::ensurePipeline() {
    if (pipeline_) {
        return;
    }
    try {
        pipeline_ = std::make_unique<::genesis::RemoverPipeline>(adapter_luid_);
        pipeline_adapter_luid_ = adapter_luid_;
    } catch (const std::exception& ex) {
        if (adapter_luid_ == 0) {
            throw;
        }
        host().log_warning(std::format(
            "GENESIS Remover pipeline failed for adapter {:016x} ({}), retrying default GPU",
            adapter_luid_, ex.what()));
        pipeline_ = std::make_unique<::genesis::RemoverPipeline>(0);
        pipeline_adapter_luid_ = 0;
        adapter_luid_ = 0;
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
        pipeline_->videoIn().recreate(static_cast<std::uint32_t>(desc.width),
                                      static_cast<std::uint32_t>(desc.height));
        pipeline_->resize(static_cast<std::uint32_t>(desc.width),
                          static_cast<std::uint32_t>(desc.height));
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
        width_ = static_cast<std::uint32_t>(desc.width);
        height_ = static_cast<std::uint32_t>(desc.height);
        publishOutput();
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
    applyMatteSettings();
    publishVideoIn();
    return true;
} catch (const std::exception& ex) {
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
    std::string error;
    if (!pipeline_->processFrame(&error)) {
        host().log_error(std::format("GENESIS Remover frame failed: {}", error));
        return RenderResult::Failed;
    }
    return RenderResult::Succeeded;
} catch (const std::exception& ex) {
    host().log_error(std::format("GENESIS Remover render failed: {}", ex.what()));
    return RenderResult::Failed;
}

SyncDesc Input::after_render() noexcept {
    return {};
}

} // namespace rxext::genesis_remover
