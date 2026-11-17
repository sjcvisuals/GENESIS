#include "Input.h"

#include "ModulePaths.h"
#include "ResourceLoader.h"
#include "SceneSelectors.h"
#include "runtime/HostTimingAdapter.hpp"
#include "runtime/RuntimePaths.hpp"
#include "protocol/OscProtocol.hpp"
#include "common/string.h"
#include "renderer/vulkan/ExternalRenderTarget.hpp"
#include "renderer/vulkan/ExternalSync.hpp"

#include <format>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <volk.h>

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
    mode_->set_property<std::vector<std::string>>(PropertyNames::enum_names,
                                                   {"Scene", "Background Remover"});
    mode_->set_value(0);

    video_in_ = add_parameter<ParameterTexture>("texture");
    video_in_->set_property(PropertyNames::name, "Video In");
    video_in_->set_property(PropertyNames::group_name, "Inputs");

    quality_ = add_parameter<ParameterInt>("quality");
    quality_->set_property(PropertyNames::name, "Quality");
    quality_->set_property(PropertyNames::group_name, "Matte");
    quality_->set_property<std::vector<std::string>>(PropertyNames::enum_names,
                                                      {"Fast", "Balanced", "Quality"});
    quality_->set_value(1);

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
        // Timeline-safe: publish shared output only. video_in / async happen in update().
        ensureRemover();
        remover_->resize(width_, height_);
        publishTargetTexture();
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
    // Async/ONNX stay deferred until after output sampler is published and video_in exists.
    host().log_info("GENESIS Background Remover Vulkan ready (matte deferred)");
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
    remover_->setSettings(matte);
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
            configureRoomOscIfNeeded(resource);
            // Fresh per-stream cursors so dual-GPU consumers replay from the shared OSC hub.
            osc_particle_cursor_ = 0;
            osc_room_person_cursor_ = 0;
            host().log_info(std::format("GENESIS loaded show '{}'", resource));
        } else {
            host().log_error(std::format("GENESIS failed to load show '{}': {}", resource,
                                         session_->lastLoadError()));
        }
    } else {
        host().log_error(std::format("GENESIS could not resolve show '{}'", resource));
    }
}

void Input::configureRoomOscIfNeeded(const std::string& resource_path) {
    if (resource_path.find("room_osc_particles") == std::string::npos) {
        return;
    }
    ::genesis::OscProtocol& osc = ::genesis::sharedOscProtocol();
    ::genesis::OscRoomPersonSettings room = osc.roomPersonSettings();
    ::genesis::configureRoomOscInstallSettings(room);
    osc.setRoomPersonSettings(room);

    ::genesis::OscParticleEmitSettings emit = osc.particleEmitSettings();
    emit.emitter_path = "Scene/Person Pool/Feet 00";
    emit.track_emitter_position = true;
    emit.burst_on_message = false;
    osc.setParticleEmitSettings(emit);
    host().log_info("GENESIS room OSC person configured for Renderbridge");
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

    auto allocate = [&](const TextureDesc& desc) {
        if (desc.width == 0 || desc.height == 0) {
            return;
        }
        width_ = static_cast<std::uint32_t>(desc.width);
        height_ = static_cast<std::uint32_t>(desc.height);
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
        publishTargetTexture();
        applyMatteSettings();
        remover_->setAsyncEnabled(true);
    };

    update_input_texture(*video_in_, allocate);

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
            allocate(desc);
        }
    }
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
        if (remover_->hasVideoIn() && !remover_->asyncEnabled()) {
            applyMatteSettings();
            remover_->setAsyncEnabled(true);
        }
        std::string error;
        if (!remover_->processFrame(&error)) {
            host().log_error(std::format("GENESIS remover failed: {}", error));
            return RenderResult::Failed;
        }
        return RenderResult::Succeeded;
    }

    actor_bindings_.apply(session_->sceneRuntime());

    // Shared OSC hub: one UDP listen per process, per-stream cursor for dual-GPU.
    ::genesis::OscProtocol& osc = ::genesis::sharedOscProtocol();
    if (osc.settings().listening || osc.isListening()) {
        osc_time_seconds_ = time_ ? time_->value() : osc_time_seconds_;
        osc.poll(osc_time_seconds_);
        osc.applyBindings(session_->sceneRuntime());
        osc.applyParticleEmits(session_->sceneRuntime(), osc_particle_cursor_);
        osc.applyRoomPerson(session_->sceneRuntime(), osc_room_person_cursor_);
    }

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
