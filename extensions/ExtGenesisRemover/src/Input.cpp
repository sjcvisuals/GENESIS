#include "Input.h"

#include "ModulePaths.h"
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

    sampler_ = add_output_parameter<ParameterTexture>(ParameterNames::sampler);

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
    return update_settings(std::move(settings_));
} catch (const std::exception& ex) {
    host().log_error(std::format("GENESIS Remover init failed: {}", ex.what()));
    return false;
}

bool Input::update_settings(ValueSet settings) noexcept try {
    settings_ = std::move(settings);
    width_ = settings_.get<std::uint32_t>(SettingNames::resolution_x, 1920);
    height_ = settings_.get<std::uint32_t>(SettingNames::resolution_y, 1080);

    if (!pipeline_) {
        pipeline_ = std::make_unique<::genesis::RemoverPipeline>(adapter_luid_);
    }
    pipeline_->resize(width_, height_);
    pipeline_->setAsyncEnabled(true);

    ::genesis::BgRemoverSettings matte{};
    matte.backend = ::genesis::BgRemoverBackend::Auto;
    const int q = quality_ ? quality_->value() : 1;
    matte.quality = static_cast<::genesis::BgRemoverQuality>(std::clamp(q, 0, 2));
    pipeline_->setSettings(matte);

    publishOutput();
    publishVideoIn();
    host().log_info(std::format("GENESIS Remover ready ({})", pipeline_->remover().status()));
    return true;
} catch (const std::exception& ex) {
    host().log_error(std::format("GENESIS Remover settings failed: {}", ex.what()));
    return false;
}

void Input::publishOutput() {
    if (!pipeline_) {
        return;
    }
    const auto& exp = pipeline_->output().colorExport();
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
        publishOutput();
    });
}

ValueSet Input::get_state() noexcept {
    ValueSet state;
    const auto desc = sampler_->texture().desc();
    state.set(StateNames::resolution_x, desc.width);
    state.set(StateNames::resolution_y, desc.height);
    state.set(StateNames::format, get_format_name(desc.format));
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
    if (pipeline_ && quality_) {
        ::genesis::BgRemoverSettings matte = pipeline_->remover().settings();
        const int q = std::clamp(quality_->value(), 0, 2);
        const auto next_q = static_cast<::genesis::BgRemoverQuality>(q);
        if (matte.quality != next_q) {
            matte.quality = next_q;
            pipeline_->setSettings(matte);
        }
    }
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
