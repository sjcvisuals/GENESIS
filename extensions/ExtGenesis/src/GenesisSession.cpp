#include "GenesisSession.h"

#include "ResourceLoader.h"
#include "renderer/vulkan/ExternalSync.hpp"
#include "scene/GnsPackage.hpp"
#include "scene/Lighting.hpp"
#include "scene/Scene.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

namespace genesis {

GenesisSession::GenesisSession(const OffscreenRendererConfig& config) {
    renderer_ = std::make_unique<OffscreenRenderer>(config);
    scene_ = std::make_unique<SceneRuntime>(renderer_->context());
    width_ = config.width;
    height_ = config.height;
}

GenesisSession::~GenesisSession() = default;

void GenesisSession::resize(std::uint32_t width, std::uint32_t height) {
    width_ = width;
    height_ = height;
    renderer_->resize(width, height);
}

void GenesisSession::loadResolved(const rxext::genesis_ext::ResolvedSceneResource& resource) {
    last_pixera_config_.reset();
    switch (resource.kind) {
    case rxext::genesis_ext::SceneResourceKind::Demo:
        scene_->loadDemo();
        break;
    case rxext::genesis_ext::SceneResourceKind::Document:
        scene_->loadDocumentFile(resource.path);
        break;
    case rxext::genesis_ext::SceneResourceKind::Gltf:
        scene_->loadGltfFile(resource.path);
        break;
    case rxext::genesis_ext::SceneResourceKind::Gns:
        last_pixera_config_ = GnsPackage::load(resource.path).pixera;
        scene_->loadGnsFile(resource.path);
        break;
    }
    scene_->updateTransforms();
}

void GenesisSession::updateTransforms() {
    scene_->updateTransforms();
}

void GenesisSession::prepareFrameUniforms(const glm::dmat4& view, const glm::dmat4& projection) {
    const glm::dmat4 view_proj = projection * view;
    glm::mat4 view_proj_f = glm::mat4(view_proj);

    glm::vec3 camera_pos{0.0f, 0.0f, 0.0f};
    if (std::optional<glm::vec3> pos = scene_->outputCameraPosition()) {
        camera_pos = *pos;
    }

    frame_uniforms_ = buildFrameUniforms(scene_->scene(), glm::value_ptr(view_proj_f), camera_pos);
}

void GenesisSession::render(const glm::dmat4& view,
                            const glm::dmat4& projection,
                            double timeline_time,
                            bool use_scene_camera) {
    (void)timeline_time;

    glm::dmat4 render_view = view;
    glm::dmat4 render_projection = projection;

    if (use_scene_camera) {
        const float aspect =
            (height_ > 0) ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f;
        if (const Node* cam_node = scene_->scene().activeOutputCameraNode();
            cam_node && cam_node->camera.has_value()) {
            const glm::mat4 view_f = cam_node->camera->viewMatrix(cam_node->world);
            const glm::mat4 proj_f = cam_node->camera->projectionMatrix(aspect);
            render_view = glm::dmat4(view_f);
            render_projection = glm::dmat4(proj_f);
        }
    }

    prepareFrameUniforms(render_view, render_projection);
    renderer_->setFrameUniforms(frame_uniforms_);
    renderer_->beginFrame();

    scene_->collectDrawCommands(draw_commands_);
    for (const SceneDrawCommand& cmd : draw_commands_) {
        renderer_->drawMesh(*cmd.mesh, cmd.constants, cmd.base_color_texture);
    }

    renderer_->endFrame(renderer_->sync().hostFinishedSemaphore());
}

} // namespace genesis
