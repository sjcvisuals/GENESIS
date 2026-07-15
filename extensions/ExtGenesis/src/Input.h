#pragma once

#include "ActorBindings.h"
#include "GenesisSession.h"
#include "ResourceLoader.h"
#include "SceneParameterCatalog.h"
#include "rxext_client.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

namespace rxext::genesis_ext {

using ParameterMatrix4 = ParameterT<glm::dmat4, ParameterType::Matrix4>;

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
    void ensureSession();
    void refreshShowSelector();
    void applyLoadedPixeraConfig();
    void reloadSceneIfNeeded();
    std::string selectedShowPath() const;

    std::uint64_t adapter_luid_ = 0;
    std::uint64_t session_adapter_luid_ = 0;
    ValueSet settings_;
    ParameterTexture* sampler_ = nullptr;
    ParameterValue* time_ = nullptr;
    ParameterMatrix4* view_matrix_ = nullptr;
    ParameterMatrix4* projection_matrix_ = nullptr;
    ParameterInt* show_select_ = nullptr;
    ParameterBool* use_scene_camera_ = nullptr;
    ActorBindings actor_bindings_;
    SceneParameterCatalog parameter_catalog_;
    std::filesystem::path module_dir_;
    std::vector<std::string> show_paths_;
    std::string loaded_scene_resource_;
    std::int32_t loaded_show_index_ = -1;
    std::unique_ptr<::genesis::GenesisSession> session_;
    std::uint32_t width_ = 1920;
    std::uint32_t height_ = 1080;
};

} // namespace rxext::genesis_ext
