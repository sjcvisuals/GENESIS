#pragma once

#include "ActorBindings.h"
#include "runtime/GenesisSession.hpp"
#include "runtime/RemoverPipeline.hpp"
#include "ResourceLoader.h"
#include "rxext_client.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

namespace rxext::genesis_ext {

using ParameterMatrix4 = ParameterT<glm::dmat4, ParameterType::Matrix4>;

enum class StreamMode : std::int32_t {
    Scene = 0,
    BackgroundRemover = 1,
};

class Input : public rxext::InputStream {
    friend class ActorBindings;

public:
    Input(std::uint64_t adapter_luid, ValueSet settings);
    ~Input() override;

    bool initialize() noexcept override;
    bool update_settings(ValueSet settings) noexcept override;
    ValueSet get_state() noexcept override;
    string get_property(string_view name) noexcept override;
    bool update() noexcept override;
    SyncDesc before_render() noexcept override;
    RenderResult render() noexcept override;
    SyncDesc after_render() noexcept override;

private:
    void publishTargetTexture();
    void publishVideoInTexture();
    void ensureSession();
    void ensureRemover();
    void applyMatteSettings();
    void refreshShowSelector();
    void refreshParameterCatalog();
    void applyLoadedPixeraConfig();
    void reloadSceneIfNeeded();
    std::string selectedShowPath() const;
    StreamMode currentMode() const;

    std::uint64_t adapter_luid_ = 0;
    std::uint64_t session_adapter_luid_ = 0;
    ValueSet settings_;
    ParameterTexture* sampler_ = nullptr;
    ParameterTexture* video_in_ = nullptr;
    ParameterInt* mode_ = nullptr;
    ParameterInt* quality_ = nullptr;
    ParameterValue* time_ = nullptr;
    ParameterMatrix4* view_matrix_ = nullptr;
    ParameterMatrix4* projection_matrix_ = nullptr;
    ParameterInt* show_select_ = nullptr;
    ParameterBool* use_scene_camera_ = nullptr;
    ParameterBool* reload_scene_ = nullptr;
    ParameterBool* show_perf_debug_ = nullptr;
    ActorBindings actor_bindings_;
    std::filesystem::path module_dir_;
    std::vector<std::string> show_paths_;
    std::string loaded_scene_resource_;
    std::int32_t loaded_show_index_ = -1;
    std::unique_ptr<::genesis::GenesisSession> session_;
    std::unique_ptr<::genesis::RemoverPipeline> remover_;
    std::uint32_t width_ = 1920;
    std::uint32_t height_ = 1080;
    std::uint64_t osc_particle_cursor_ = 0;
    std::uint64_t osc_room_person_cursor_ = 0;
    double osc_time_seconds_ = 0.0;

    void configureRoomOscIfNeeded(const std::string& resource_path);
};

} // namespace rxext::genesis_ext
