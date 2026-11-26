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

    // Notch-like clip: Scene picker only. Background Remover is a separate product/DLL.
    ParameterVector3* position_param = nullptr;
    actor_bindings_.registerSceneParameters(*this, show_select_, use_scene_camera_, reload_scene_,
                                              position_param);

    show_perf_debug_ = add_parameter<ParameterBool>("show_perf_debug", false);
    show_perf_debug_->set_property(PropertyNames::name, "Show Perf Debug");
    show_perf_debug_->set_property(PropertyNames::group_name, "Debug");

    view_matrix_ = add_parameter<ParameterMatrix4>("view_matrix");
    view_matrix_->set_property(PropertyNames::purpose, PurposeNames::ViewMatrix);
    view_matrix_->set_property(PropertyNames::name, "View Matrix");
    view_matrix_->set_property(PropertyNames::group_name, "Camera");

    projection_matrix_ = add_parameter<ParameterMatrix4>("projection_matrix");
    projection_matrix_->set_property(PropertyNames::purpose, PurposeNames::ProjectionMatrix);
    projection_matrix_->set_property(PropertyNames::name, "Projection Matrix");
    projection_matrix_->set_property(PropertyNames::group_name, "Camera");

    time_ = add_parameter<ParameterValue>("time");
    time_->set_property(PropertyNames::purpose, PurposeNames::TimelineTime);

    refreshShowSelector();
}

Input::~Input() = default;

bool Input::initialize() noexcept try {
    host().log_info(std::format("initializing GENESIS stream '{}' ({} packaged scenes)",
                                settings_.get(SettingNames::handle), show_paths_.size()));
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
    const std::vector<std::string> scanned = ::genesis::scanRuntimeShowResources(module_dir_);
    const std::int32_t previous_index = show_select_ ? show_select_->value() : 0;
    const std::string previous_resource = show_paths_.empty() ? "demo" : selectedShowPath();

    show_paths_ = scanned;
    const std::int32_t restored_index =
        findSelectorIndex(show_paths_, previous_resource, previous_index);
    setShowEnumOptions(show_select_, show_paths_, restored_index);
}

void Input::refreshParameterCatalog() {
    // Only refresh the scene dropdown when packaged shows appear/disappear beside the DLL.
    const std::vector<std::string> scanned = ::genesis::scanRuntimeShowResources(module_dir_);
    if (scanned == show_paths_) {
        return;
    }
    refreshShowSelector();
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
        return;
    }

    if (!pixera->streams.empty() && use_scene_camera_) {
        use_scene_camera_->set_value(pixera->streams.front().use_scene_camera);
    }

    if (show_select_) {
        const std::int32_t show_index =
            findSelectorIndex(show_paths_, loaded_scene_resource_, show_select_->value());
        setShowEnumOptions(show_select_, show_paths_, show_index);
    }

    if (!pixera->bindings.target_node.empty()) {
        host().log_info(std::format("GENESIS target_node (deferred expose): {}",
                                    pixera->bindings.target_node));
    }
    if (!pixera->bindings.light_node.empty()) {
        host().log_info(std::format("GENESIS light_node (deferred expose): {}",
                                    pixera->bindings.light_node));
    }
}

void Input::applyLoadedSceneOscSettings() {
    if (!session_) {
        return;
    }

    const std::optional<::genesis::SceneOscSettings>& scene_osc =
        session_->sceneRuntime().oscSettings();
    if (!scene_osc.has_value()) {
        return;
    }

    ::genesis::OscProtocol& osc = ::genesis::sharedOscProtocol();
    std::string error;
    if (!osc.applySceneOscSettings(*scene_osc, &error)) {
        host().log_error(std::format("GENESIS scene OSC listen failed ({}:{}): {}",
                                     scene_osc->bind_ip, scene_osc->port, error));
        return;
    }
    host().log_info(std::format("GENESIS scene OSC {}:{} (listening={})", scene_osc->bind_ip,
                                scene_osc->port, scene_osc->listening ? "yes" : "no"));
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
            applyLoadedPixeraConfig();
            applyLoadedSceneOscSettings();
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
    const bool room = resource_path.find("room_osc_particles") != std::string::npos;
    const bool medusa = resource_path.find("medusa") != std::string::npos; // medusa.gns / medusa.scene.json
    if (!room && !medusa) {
        return;
    }
    ::genesis::OscProtocol& osc = ::genesis::sharedOscProtocol();
    ::genesis::OscRoomPersonSettings room_settings = osc.roomPersonSettings();
    if (medusa) {
        ::genesis::configureMedusaExampleSettings(room_settings);
        // Renderbridge output: keep wall-ray math, skip editor line-trace overlay.
        room_settings.debug_draw_wall_ray = false;
    } else {
        ::genesis::configureRoomOscInstallSettings(room_settings);
    }
    osc.setRoomPersonSettings(room_settings);

    ::genesis::OscParticleEmitSettings emit = osc.particleEmitSettings();
    emit.emitter_path = "Scene/Person Pool/Feet 00";
    emit.track_emitter_position = true;
    emit.burst_on_message = false;
    osc.setParticleEmitSettings(emit);
    host().log_info(medusa ? "GENESIS Medusa OSC person configured for Renderbridge"
                           : "GENESIS room OSC person configured for Renderbridge");
}

void Input::publishTargetTexture() {
    if (!session_) {
        return;
    }

    const ::genesis::ExternalImageExport& export_info = session_->renderer().target().colorExport();
    const std::uint32_t w = session_->renderer().target().extent().width;
    const std::uint32_t h = session_->renderer().target().extent().height;

    TextureDesc desc{};
    desc.width = w;
    desc.height = h;
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
    return {
        SyncStrategy::BinarySemaphore,
        ShareHandle{
            kHandleType,
            reinterpret_cast<Handle>(session_->renderer().sync().hostFinishedHandle()),
        },
    };
}

RenderResult Input::render() noexcept try {
    // Shared OSC hub: one UDP listen per process, per-stream cursor for dual-GPU.
    ::genesis::OscProtocol& osc = ::genesis::sharedOscProtocol();
    if (osc.settings().listening || osc.isListening()) {
        osc_time_seconds_ = time_ ? time_->value() : osc_time_seconds_;
        osc.poll(osc_time_seconds_);
        osc.applyBindings(session_->sceneRuntime());
        osc.applyParticleEmits(session_->sceneRuntime(), osc_particle_cursor_);
        osc.applyRoomPerson(session_->sceneRuntime(), osc_room_person_cursor_);
    }

    if (show_perf_debug_) {
        session_->setShowPerfOnOutput(show_perf_debug_->value());
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
    return {
        SyncStrategy::BinarySemaphore,
        ShareHandle{
            kHandleType,
            reinterpret_cast<Handle>(session_->renderer().sync().renderFinishedHandle()),
        },
    };
}

} // namespace rxext::genesis_ext
