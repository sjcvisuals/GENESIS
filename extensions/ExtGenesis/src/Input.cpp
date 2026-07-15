#include "Input.h"

#include "ModulePaths.h"
#include "SceneSelectors.h"
#include "common/string.h"
#include "renderer/vulkan/ExternalRenderTarget.hpp"
#include "renderer/vulkan/ExternalSync.hpp"

#include <format>
#include <glm/gtc/matrix_transform.hpp>

namespace rxext::genesis_ext {

namespace {
#if defined(_WIN32)
constexpr auto kHandleType = HandleType::OPAQUE_WIN32;
#else
constexpr auto kHandleType = HandleType::OPAQUE_FD;
#endif

glm::dmat4 defaultViewMatrix() {
    return glm::lookAt(glm::dvec3(0.0, -1.0, -2.0), glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0));
}

glm::dmat4 defaultProjectionMatrix(std::uint32_t width, std::uint32_t height) {
    const float aspect = (height > 0) ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    return glm::dmat4(glm::perspectiveRH_ZO(glm::radians(45.0f), aspect, 0.1f, 100.0f));
}

} // namespace

Input::Input(std::uint64_t adapter_luid, ValueSet settings)
    : adapter_luid_(adapter_luid), settings_(std::move(settings)), module_dir_(extensionModuleDirectory()) {
    sampler_ = add_output_parameter<ParameterTexture>(ParameterNames::sampler);

    actor_bindings_.registerSceneParameters(*this, show_select_, use_scene_camera_);

    view_matrix_ = add_parameter<ParameterMatrix4>("view_matrix");
    view_matrix_->set_property(PropertyNames::purpose, PurposeNames::ViewMatrix);
    view_matrix_->set_property(PropertyNames::name, "View Matrix");
    view_matrix_->set_property(PropertyNames::group_name, "Camera");

    projection_matrix_ = add_parameter<ParameterMatrix4>("projection_matrix");
    projection_matrix_->set_property(PropertyNames::purpose, PurposeNames::ProjectionMatrix);
    projection_matrix_->set_property(PropertyNames::name, "Projection Matrix");
    projection_matrix_->set_property(PropertyNames::group_name, "Camera");

    parameter_catalog_ = buildSceneParameterCatalog(module_dir_);
    actor_bindings_.registerActors(*this, parameter_catalog_);

    time_ = add_parameter<ParameterValue>("time");
    time_->set_property(PropertyNames::purpose, PurposeNames::TimelineTime);

    refreshShowSelector();
}

Input::~Input() = default;

bool Input::initialize() noexcept try {
    host().log_info(std::format("initializing GENESIS stream '{}' ({} actors, {} lights)",
                                settings_.get(SettingNames::handle),
                                actor_bindings_.actorCount(),
                                actor_bindings_.lightCount()));
    if (!update_settings(std::move(settings_))) {
        return false;
    }
    return true;
} catch (const std::exception& ex) {
    host().log_error(std::format("initializing GENESIS input failed: {}", ex.what()));
    return false;
}

bool Input::update_settings(ValueSet settings) noexcept try {
    settings_ = std::move(settings);
    width_ = settings_.get<std::uint32_t>(SettingNames::resolution_x, 1920);
    height_ = settings_.get<std::uint32_t>(SettingNames::resolution_y, 1080);

    if (const string stream_luid = settings_.get(SettingNames::adapter_luid, string{}); !stream_luid.empty()) {
        adapter_luid_ = common::from_hex_string<std::uint64_t>(stream_luid);
    }

    if (session_ && session_adapter_luid_ != adapter_luid_) {
        session_.reset();
        loaded_scene_resource_.clear();
        loaded_show_index_ = -1;
    }

    ensureSession();
    session_->resize(width_, height_);
    publishTargetTexture();

    view_matrix_->set_value(defaultViewMatrix());
    projection_matrix_->set_value(defaultProjectionMatrix(width_, height_));
    reloadSceneIfNeeded();
    return true;
} catch (const std::exception& ex) {
    host().log_error(std::format("updating GENESIS stream settings failed: {}", ex.what()));
    return false;
}

void Input::ensureSession() {
    if (session_) {
        return;
    }

    ::genesis::OffscreenRendererConfig config{};
    config.adapter_luid = adapter_luid_;
    config.width = width_;
    config.height = height_;
    config.enable_validation = false;

    session_ = std::make_unique<::genesis::GenesisSession>(config);
    session_adapter_luid_ = adapter_luid_;
}

void Input::refreshShowSelector() {
    show_paths_ = scanShowResources(module_dir_);
    const std::int32_t demo_index =
        findSelectorIndex(show_paths_, "scenes/demo.gns", findSelectorIndex(show_paths_, "demo", 0));
    setShowEnumOptions(show_select_, show_paths_, demo_index);
}

std::string Input::selectedShowPath() const {
    if (show_paths_.empty()) {
        return "demo";
    }
    const std::int32_t index = show_select_->value();
    const std::int32_t clamped =
        std::clamp(index, std::int32_t{0}, static_cast<std::int32_t>(show_paths_.size() - 1));
    return show_paths_[static_cast<std::size_t>(clamped)];
}

void Input::applyLoadedPixeraConfig() {
    if (!session_) {
        return;
    }

    const std::optional<::genesis::GnsPixeraConfig>& pixera = session_->pixeraConfig();
    if (!pixera.has_value()) {
        return;
    }

    if (!pixera->streams.empty()) {
        use_scene_camera_->set_value(pixera->streams.front().use_scene_camera);
    }

    const std::int32_t show_index = findSelectorIndex(show_paths_, loaded_scene_resource_, show_select_->value());
    setShowEnumOptions(show_select_, show_paths_, show_index);
}

void Input::reloadSceneIfNeeded() {
    if (!session_) {
        return;
    }

    const std::int32_t show_index = show_select_->value();
    const std::string resource = selectedShowPath();
    if (resource == loaded_scene_resource_ && show_index == loaded_show_index_) {
        return;
    }

    if (const std::optional<ResolvedSceneResource> resolved =
            resolveSceneResource(host(), resource, module_dir_)) {
        session_->loadResolved(*resolved);
        loaded_scene_resource_ = resource;
        loaded_show_index_ = show_index;
        applyLoadedPixeraConfig();
        host().log_info(std::format("GENESIS loaded show '{}'", resource));
    }
}

void Input::publishTargetTexture() {
    const auto& target = session_->renderer().target();
    const auto& export_info = target.colorExport();

    TextureDesc desc{};
    desc.width = target.extent().width;
    desc.height = target.extent().height;
    desc.format = Format::R8G8B8A8_UNORM;
    desc.is_target = true;
    desc.share_handle = {
        kHandleType,
        reinterpret_cast<Handle>(export_info.win32_handle),
        0,
        export_info.memory_size,
        0,
        export_info.dedicated,
    };

    sampler_->set_texture(host().create_texture(desc));
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
    reloadSceneIfNeeded();
    return true;
} catch (const std::exception& ex) {
    host().log_error(std::format("GENESIS update failed: {}", ex.what()));
    return false;
}

SyncDesc Input::before_render() noexcept {
    return {
        SyncStrategy::BinarySemaphore,
        ShareHandle{
            kHandleType,
            reinterpret_cast<Handle>(session_->renderer().sync().hostFinishedHandle()),
        },
    };
}

RenderResult Input::render() noexcept try {
    actor_bindings_.apply(session_->sceneRuntime());
    session_->updateTransforms();
    session_->render(view_matrix_->value(),
                     projection_matrix_->value(),
                     time_->value(),
                     use_scene_camera_->value());
    return RenderResult::Succeeded;
} catch (const std::exception& ex) {
    host().log_error(std::format("GENESIS render failed: {}", ex.what()));
    return RenderResult::Failed;
}

SyncDesc Input::after_render() noexcept {
    return {
        SyncStrategy::BinarySemaphore,
        ShareHandle{
            kHandleType,
            reinterpret_cast<Handle>(session_->renderer().sync().renderFinishedHandle()),
        },
    };
}

} // namespace rxext::genesis_ext
