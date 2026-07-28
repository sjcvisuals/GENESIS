#include "Input.h"

#include "ModulePaths.h"
#include "ResourceLoader.h"
#include "SceneSelectors.h"
#include "runtime/HostTimingAdapter.hpp"
#include "runtime/RuntimePaths.hpp"
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
    ::genesis::setRuntimeAssetRoots(module_dir_);

    sampler_ = add_output_parameter<ParameterTexture>(ParameterNames::sampler);

    mode_ = add_parameter<ParameterInt>("mode");
    mode_->set_property(PropertyNames::name, "Mode");
    mode_->set_property(PropertyNames::group_name, "Stream");
    mode_->set_property(PropertyNames::enum_names, {"Scene", "Background Remover"});
    mode_->set_value(0);

    video_in_ = add_parameter<ParameterTexture>("texture");
    video_in_->set_property(PropertyNames::name, "Video In");
    video_in_->set_property(PropertyNames::group_name, "Inputs");

    ParameterVector3* position_param_ = nullptr;
    actor_bindings_.registerSceneParameters(*this, show_select_, use_scene_camera_, reload_scene_,
                                              position_param_);

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
        remover_.reset();
        loaded_scene_resource_.clear();
        loaded_show_index_ = -1;
    }

    if (currentMode() == StreamMode::BackgroundRemover) {
        ensureRemover();
        remover_->resize(width_, height_);
        publishTargetTexture();
        publishVideoInTexture();
    } else {
        ensureSession();
        session_->resize(width_, height_);
        publishTargetTexture();
        view_matrix_->set_value(defaultViewMatrix());
        projection_matrix_->set_value(defaultProjectionMatrix(width_, height_));
        reloadSceneIfNeeded();
    }
    return true;
} catch (const std::exception& ex) {
    host().log_error(std::format("updating GENESIS stream settings failed: {}", ex.what()));
    return false;
}

StreamMode Input::currentMode() const {
    const std::int32_t mode = mode_ ? mode_->value() : 0;
    return mode == static_cast<std::int32_t>(StreamMode::BackgroundRemover)
               ? StreamMode::BackgroundRemover
               : StreamMode::Scene;
}

void Input::ensureRemover() {
    if (remover_) {
        return;
    }
    remover_ = std::make_unique<::genesis::RemoverPipeline>(adapter_luid_);
    host().log_info(std::format("GENESIS Background Remover ready ({})",
                                remover_->remover().status()));
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
    const std::vector<std::string> scanned = ::genesis::scanRuntimeShowResources(module_dir_);
    const std::int32_t previous_index = show_select_ ? show_select_->value() : 0;
    const std::string previous_resource = show_paths_.empty() ? "demo" : selectedShowPath();

    show_paths_ = scanned;
    const std::int32_t restored_index =
        findSelectorIndex(show_paths_, previous_resource, previous_index);
    setShowEnumOptions(show_select_, show_paths_, restored_index);
}

void Input::refreshParameterCatalog() {
    const std::vector<std::string> scanned = ::genesis::scanRuntimeShowResources(module_dir_);
    const bool show_list_changed = scanned != show_paths_;
    if (!show_list_changed) {
        return;
    }

    parameter_catalog_ = buildSceneParameterCatalog(module_dir_);
    const std::size_t added = actor_bindings_.registerNewFromCatalog(*this, parameter_catalog_);
    if (added > 0) {
        host().log_info(std::format("GENESIS registered {} new actor/light parameters", added));
    }

    refreshShowSelector();
}

void Input::refreshCatalogForLoadedShow() {
    if (loaded_scene_resource_.empty()) {
        return;
    }

    const SceneParameterCatalog show_catalog =
        buildSceneParameterCatalogForShow(module_dir_, loaded_scene_resource_);
    const std::size_t added = actor_bindings_.registerNewFromCatalog(*this, show_catalog);
    if (added > 0) {
        host().log_info(std::format("GENESIS registered {} parameters for '{}'", added,
                                    loaded_scene_resource_));
    }
}

std::string Input::selectedShowPath() const {
    if (show_paths_.empty()) {
        return "demo";
    }
    const std::int32_t index = show_select_ ? show_select_->value() : 0;
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
        ::genesis::GnsBindings empty{};
        actor_bindings_.applyGnsBindings(empty, session_->sceneRuntime());
        actor_bindings_.syncAllFromScene(session_->sceneRuntime());
        return;
    }

    if (!pixera->streams.empty()) {
        use_scene_camera_->set_value(pixera->streams.front().use_scene_camera);
    }

    if (show_select_) {
        const std::int32_t show_index =
            findSelectorIndex(show_paths_, loaded_scene_resource_, show_select_->value());
        setShowEnumOptions(show_select_, show_paths_, show_index);
    }

    actor_bindings_.applyGnsBindings(pixera->bindings, session_->sceneRuntime());
    actor_bindings_.syncAllFromScene(session_->sceneRuntime());

    if (!pixera->bindings.target_node.empty()) {
        host().log_info(std::format("GENESIS target_node: {}", pixera->bindings.target_node));
    }
    if (!pixera->bindings.light_node.empty()) {
        host().log_info(std::format("GENESIS light_node: {}", pixera->bindings.light_node));
    }
}

void Input::reloadSceneIfNeeded() {
    if (!session_) {
        return;
    }

    const std::string resource = selectedShowPath();
    const std::int32_t show_index = show_select_ ? show_select_->value() : 0;
    if (resource == loaded_scene_resource_ && show_index == loaded_show_index_) {
        return;
    }

    if (const std::optional<ResolvedSceneResource> resolved =
            resolveSceneResource(host(), resource, module_dir_)) {
        if (session_->tryLoadResolved(toGenesisRuntimeResource(*resolved))) {
            loaded_scene_resource_ = resource;
            loaded_show_index_ = show_index;
            refreshCatalogForLoadedShow();
            applyLoadedPixeraConfig();
            host().log_info(std::format("GENESIS loaded show '{}'", resource));
        } else {
            host().log_error(std::format("GENESIS failed to load show '{}': {}", resource,
                                         session_->lastLoadError()));
        }
    } else {
        host().log_error(std::format("GENESIS could not resolve show '{}'", resource));
    }
}

void Input::publishTargetTexture() {
    const ::genesis::ExternalImageExport* export_info = nullptr;
    std::uint32_t w = width_;
    std::uint32_t h = height_;

    if (currentMode() == StreamMode::BackgroundRemover && remover_) {
        export_info = &remover_->output().colorExport();
        w = remover_->output().extent().width;
        h = remover_->output().extent().height;
    } else if (session_) {
        export_info = &session_->renderer().target().colorExport();
        w = session_->renderer().target().extent().width;
        h = session_->renderer().target().extent().height;
    } else {
        return;
    }

    TextureDesc desc{};
    desc.width = w;
    desc.height = h;
    desc.format = Format::R8G8B8A8_UNORM;
    desc.is_target = true;
    desc.share_handle = {
        kHandleType,
        reinterpret_cast<Handle>(export_info->win32_handle),
        0,
        export_info->memory_size,
        0,
        export_info->dedicated,
    };

    sampler_->set_texture(host().create_texture(desc));
}

void Input::publishVideoInTexture() {
    if (!video_in_ || !remover_) {
        return;
    }
    update_input_texture(*video_in_, [&](const TextureDesc& desc) {
        remover_->videoIn().recreate(static_cast<std::uint32_t>(desc.width),
                                     static_cast<std::uint32_t>(desc.height));
        const auto& exp = remover_->videoIn().exportInfo();
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
        remover_->resize(width_, height_);
        publishTargetTexture();
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
    if (currentMode() == StreamMode::BackgroundRemover) {
        ensureRemover();
        publishVideoInTexture();
        return true;
    }

    if (reload_scene_ && reload_scene_->value()) {
        loaded_scene_resource_.clear();
        loaded_show_index_ = -1;
        reload_scene_->set_value(false);
        host().log_info("GENESIS manual scene reload requested");
    }

    refreshParameterCatalog();
    reloadSceneIfNeeded();
    return true;
} catch (const std::exception& ex) {
    host().log_error(std::format("GENESIS update failed: {}", ex.what()));
    return false;
}

SyncDesc Input::before_render() noexcept {
    if (currentMode() == StreamMode::BackgroundRemover) {
        return {};
    }
    return {
        SyncStrategy::BinarySemaphore,
        ShareHandle{
            kHandleType,
            reinterpret_cast<Handle>(session_->renderer().sync().hostFinishedHandle()),
        },
    };
}

RenderResult Input::render() noexcept try {
    if (currentMode() == StreamMode::BackgroundRemover) {
        ensureRemover();
        std::string error;
        if (!remover_->processFrame(&error)) {
            host().log_error(std::format("GENESIS remover failed: {}", error));
            return RenderResult::Failed;
        }
        return RenderResult::Succeeded;
    }

    actor_bindings_.apply(session_->sceneRuntime());
    const ::genesis::HostTimingInput timing =
        ::genesis::hostTimingFromPixeraTimeline(time_->value());
    const ::genesis::GenesisFrameTime frame_time = session_->advanceProductionTime(timing);
    session_->tickFrame(frame_time);
    session_->render(view_matrix_->value(),
                     projection_matrix_->value(),
                     frame_time,
                     use_scene_camera_->value());
    return RenderResult::Succeeded;
} catch (const std::exception& ex) {
    host().log_error(std::format("GENESIS render failed: {}", ex.what()));
    return RenderResult::Failed;
}

SyncDesc Input::after_render() noexcept {
    if (currentMode() == StreamMode::BackgroundRemover) {
        return {};
    }
    return {
        SyncStrategy::BinarySemaphore,
        ShareHandle{
            kHandleType,
            reinterpret_cast<Handle>(session_->renderer().sync().renderFinishedHandle()),
        },
    };
}

} // namespace rxext::genesis_ext
